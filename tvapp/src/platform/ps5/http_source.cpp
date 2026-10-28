// ps5cc - The console app's data over HTTP (127.0.0.1:8086), on a thread of its own.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plain HTTP/1.1 with "Connection: close", one request at a time, from one
// worker thread: console threads run first-in first-out and are never
// time-sliced, so nothing here may run on the frame. Every call of the screen
// only queues a request or copies the last data.

#include "platform/ps5/http_source.hpp"

#include "platform/ps5/system.hpp"
#include "ps5cc/covers.hpp"
#include "third_party/cjson/cJSON.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

extern "C"
{
    // The app's ELF, embedded by src/platform/ps5/payload_blob.c.
    extern const unsigned char ps5cc_payload_elf[];
    extern const unsigned char ps5cc_payload_elf_end[];
}

namespace hui::ps5cc
{

namespace
{

constexpr int kPortApp = 8086;
constexpr int kPortLoader = 9021;
constexpr std::int64_t kStartLimitUs = 30 * 1000000LL;
constexpr std::size_t kMaxBody = 4u * 1024u * 1024u;

enum class Net
{
    ok,
    refused, // nothing listens: the app is not running
    failed,  // it listens but the exchange went wrong (timeout, reset)
};

int open_local(int port, int timeout_s, Net *why)
{
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        *why = Net::failed;
        return -1;
    }
    timeval tv{};
    tv.tv_sec = timeout_s;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    sockaddr_in sa{};
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<std::uint16_t>(port));
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, reinterpret_cast<const sockaddr *>(&sa), sizeof(sa)) != 0)
    {
        *why = errno == ECONNREFUSED ? Net::refused : Net::failed;
        close(s);
        return -1;
    }
    *why = Net::ok;
    return s;
}

// This title's id from its own param.json ("FAKE86921"), read once; "" when it cannot be read.
const std::string &own_title_id()
{
    static const std::string id = [] {
        std::string out;
        FILE *f = std::fopen("/app0/sce_sys/param.json", "rb");
        if (f == nullptr)
            return out;
        std::string text;
        char buf[4096];
        std::size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < 256u * 1024u)
            text.append(buf, n);
        std::fclose(f);
        cJSON *root = cJSON_Parse(text.c_str());
        const cJSON *tid = root != nullptr ? cJSON_GetObjectItemCaseSensitive(root, "titleId") : nullptr;
        if (cJSON_IsString(tid) && tid->valuestring != nullptr)
            out = tid->valuestring;
        cJSON_Delete(root);
        return out;
    }();
    return id;
}

bool send_all(int s, const void *data, std::size_t size)
{
    const auto *p = static_cast<const unsigned char *>(data);
    while (size > 0)
    {
        const ssize_t sent = send(s, p, size, 0);
        if (sent <= 0)
            return false;
        p += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

// One request. *status is the HTTP status, *body what followed the headers.
Net exchange(const char *method, const char *path, const std::string &payload, int *status,
             std::string *body, int timeout_s = 4)
{
    Net why = Net::failed;
    const int s = open_local(kPortApp, timeout_s, &why);
    if (s < 0)
        return why;
    char head[512];
    const int n = std::snprintf(head, sizeof(head),
                                "%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n"
                                "Content-Type: application/json\r\nContent-Length: %zu\r\n\r\n",
                                method, path, kPortApp, payload.size());
    if (n <= 0 || !send_all(s, head, static_cast<std::size_t>(n)) ||
        (!payload.empty() && !send_all(s, payload.data(), payload.size())))
    {
        close(s);
        return Net::failed;
    }
    std::string reply;
    char buffer[16384];
    for (;;)
    {
        const ssize_t got = recv(s, buffer, sizeof(buffer), 0);
        if (got <= 0)
            break;
        reply.append(buffer, static_cast<std::size_t>(got));
        if (reply.size() > kMaxBody)
            break;
    }
    close(s);
    const std::size_t end = reply.find("\r\n\r\n");
    if (end == std::string::npos || reply.compare(0, 5, "HTTP/") != 0)
        return Net::failed;
    const std::size_t space = reply.find(' ');
    *status = space == std::string::npos ? 0 : std::atoi(reply.c_str() + space + 1);
    *body = reply.substr(end + 4);
    return Net::ok;
}

std::string str(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

double num(const cJSON *object, const char *name, double fallback = 0.0)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

bool flag(const cJSON *object, const char *name)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(object, name));
}

std::string message_of(const std::string &body, const char *fallback)
{
    cJSON *root = cJSON_Parse(body.c_str());
    std::string text = root != nullptr ? str(root, "message") : "";
    cJSON_Delete(root);
    return text.empty() ? fallback : text;
}

void *thread_main(void *self)
{
    static_cast<HttpSource *>(self)->run();
    return nullptr;
}

void *tools_main(void *self)
{
    static_cast<HttpSource *>(self)->run_tools();
    return nullptr;
}

// A title id for a query string: letters, digits, '-' and '_' as they are, everything else percent-encoded.
std::string url_encoded(const std::string &text)
{
    std::string out;
    for (const char c : text)
    {
        const auto u = static_cast<unsigned char>(c);
        if ((u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u == '-' || u == '_')
        {
            out += c;
            continue;
        }
        char hex[4];
        std::snprintf(hex, sizeof(hex), "%%%02X", u);
        out += hex;
    }
    return out;
}

} // namespace

void HttpSource::start()
{
    data_.link = Link::starting;
    data_.link_detail = "Verbindung zur App …";
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    const int rc = pthread_create(&thread, &attr, thread_main, this);
    pthread_t tools;
    const int rc_tools = pthread_create(&tools, &attr, tools_main, this);
    pthread_attr_destroy(&attr);
    sys::log("[PS5CC] http thread rc=%d tools rc=%d", rc, rc_tools);
}

bool HttpSource::fetch(Snapshot &out, std::uint64_t &seen)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (seen == data_.serial)
        return false;
    out = data_;
    seen = data_.serial;
    return true;
}

bool HttpSource::poll_notice(Notice &out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (notices_.empty())
        return false;
    out = notices_.front();
    notices_.pop_front();
    return true;
}

void HttpSource::push(Request request)
{
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.push_back(std::move(request));
}

void HttpSource::set_target(int celsius)
{
    push({Request::Kind::target, celsius, {}, {}});
}
void HttpSource::launch(const std::string &id)
{
    push({Request::Kind::launch, 0, id, {}});
}
void HttpSource::close_game(const std::string &id)
{
    push({Request::Kind::close, 0, id, {}});
}
void HttpSource::cancel_job(JobKind kind)
{
    push({Request::Kind::cancel, static_cast<int>(kind), {}, {}});
}
void HttpSource::open_web_ui()
{
    push({Request::Kind::web, 0, {}, {}});
}
void HttpSource::reload()
{
    push({Request::Kind::reload, 0, {}, {}});
}
void HttpSource::retry()
{
    retry_ = true;
}
void HttpSource::watch(Watch page)
{
    watching_ = page;
}
void HttpSource::save_config(const std::string &patch)
{
    push({Request::Kind::config, 0, {}, patch});
}
void HttpSource::selftest(bool start)
{
    push({Request::Kind::selftest, start ? 1 : 0, {}, {}});
}
void HttpSource::power(const std::string &action)
{
    push({Request::Kind::power, 0, action, {}});
}
void HttpSource::start_payload(const PayloadFile &file)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "source", file.source.c_str());
    if (file.source == "usb")
        cJSON_AddStringToObject(root, "mount", file.mount.c_str());
    cJSON_AddStringToObject(root, "dir", file.dir.c_str());
    cJSON_AddStringToObject(root, "name", file.name.c_str());
    char *text = cJSON_PrintUnformatted(root);
    push({Request::Kind::payload_start, 0, file.name, text != nullptr ? text : "{}"});
    cJSON_free(text);
    cJSON_Delete(root);
}
void HttpSource::kill_payload(int pid)
{
    push({Request::Kind::payload_kill, pid, {}, {}});
}
void HttpSource::store_action(const std::string &action, const std::string &id)
{
    push({Request::Kind::store, 0, id, action});
}
void HttpSource::quit_app()
{
    sys::quit();
}
std::uint64_t HttpSource::request_plan(Tool tool, const std::string &id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ToolRequest r;
    r.tool = tool;
    r.id = id;
    r.ticket = ++next_ticket_;
    // Only the newest plan matters: one asked for earlier and still waiting is dropped.
    tool_requests_.erase(std::remove_if(tool_requests_.begin(), tool_requests_.end(),
                                        [](const ToolRequest &q) { return !q.start; }),
                         tool_requests_.end());
    tool_requests_.push_back(std::move(r));
    return next_ticket_;
}
void HttpSource::start_tool(Tool tool, const std::string &id, const std::string &body)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ToolRequest r;
    r.start = true;
    r.tool = tool;
    r.id = id;
    r.body = body;
    tool_requests_.push_back(std::move(r));
}

void HttpSource::run_tools()
{
    for (;;)
    {
        ToolRequest request;
        bool have = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!tool_requests_.empty())
            {
                request = tool_requests_.front();
                tool_requests_.pop_front();
                have = true;
            }
        }
        if (have)
            handle_tool(request);
        else
            sys::sleep_us(100000);
    }
}

void HttpSource::handle_tool(const ToolRequest &request)
{
    int status = 0;
    std::string body;
    const std::string id = url_encoded(request.id);
    if (!request.start)
    {
        std::string path;
        const char *method = "GET";
        std::string payload;
        switch (request.tool)
        {
        case Tool::copy:
            path = "/api/v1/library/copy/plan?id=" + id;
            break;
        case Tool::convert:
            path = "/api/v1/library/convert/plan?id=" + id;
            break;
        case Tool::move:
        case Tool::unpack:
            path = "/api/v1/library/storage/plan?id=" + id + "&op=" + (request.tool == Tool::move ? "move" : "unpack");
            break;
        case Tool::image_unpack:
            path = "/api/v1/library/image/plan?id=" + id;
            break;
        case Tool::erase:
        {
            path = "/api/v1/library/delete/plan";
            method = "POST";
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "kind", "game");
            cJSON_AddStringToObject(root, "id", request.id.c_str());
            char *text = cJSON_PrintUnformatted(root);
            payload = text != nullptr ? text : "{}";
            cJSON_free(text);
            cJSON_Delete(root);
            break;
        }
        default: // check and repair read the image's description
            path = "/api/v1/library/image?id=" + id;
            break;
        }
        // Two minutes, as the web app waits: the plan measures the whole game.
        const Net net = exchange(method, path.c_str(), payload, &status, &body, 120);
        PlanReply reply;
        reply.ticket = request.ticket;
        reply.tool = request.tool;
        reply.id = request.id;
        reply.ok = net == Net::ok && status == 200;
        if (reply.ok)
            reply.json = std::move(body);
        else
            reply.error = net == Net::ok ? message_of(body, "Die App hat keinen Plan geliefert.")
                                         : "Die App hat nicht rechtzeitig geantwortet.";
        std::lock_guard<std::mutex> lock(mutex_);
        data_.plan = std::move(reply);
        ++data_.serial;
        return;
    }

    static const char *const kStart[] = {"/api/v1/library/copy",    "/api/v1/library/convert",
                                         "/api/v1/library/storage", "/api/v1/library/storage",
                                         "/api/v1/library/image",   "/api/v1/library/image",
                                         "/api/v1/library/image",   "/api/v1/library/delete"};
    const int index = std::clamp(static_cast<int>(request.tool), 0, 7);
    const Net net = exchange("POST", kStart[index], request.body, &status, &body, 20);
    const std::string name = "„" + game_name(request.id) + "“";
    std::string label = "Auftrag";
    switch (request.tool)
    {
    case Tool::copy:
        label = "Kopieren";
        break;
    case Tool::convert:
        label = "Konvertieren";
        break;
    case Tool::move:
        label = "Verschieben";
        break;
    case Tool::unpack:
    case Tool::image_unpack:
        label = "Entpacken";
        break;
    case Tool::image_check:
        label = "Prüfen";
        break;
    case Tool::image_repair:
        label = "Reparieren";
        break;
    case Tool::erase:
        label = "Löschen";
        break;
    case Tool::count:
        break;
    }
    if (net != Net::ok || status < 200 || status >= 300)
    {
        // Busy, other_busy, a running game, a target that is gone: the app's own words (409 and the rest).
        notice(Notice::Kind::error, label + " nicht gestartet",
               net == Net::ok ? message_of(body, "Unbekannter Fehler.") : "Die App hat nicht geantwortet.");
        return;
    }
    if (request.tool == Tool::move || request.tool == Tool::unpack)
    {
        cJSON *reply = cJSON_Parse(body.c_str());
        const double job_id = num(reply, "job_id");
        if (job_id > 0.0)
            storage_seen_ = static_cast<std::int64_t>(job_id);
        cJSON_Delete(reply);
    }
    Notice n;
    n.kind = Notice::Kind::job_started;
    n.title = label + " gestartet";
    n.body = name;
    switch (request.tool)
    {
    case Tool::copy:
        n.job = JobKind::copy;
        break;
    case Tool::convert:
        n.job = JobKind::convert;
        break;
    case Tool::move:
    case Tool::unpack:
        n.job = JobKind::storage;
        break;
    case Tool::erase:
        n.job = JobKind::deletion;
        break;
    default:
        n.job = JobKind::image;
        break;
    }
    jobs_wanted_ = true;
    std::lock_guard<std::mutex> lock(mutex_);
    notices_.push_back(std::move(n));
}

void HttpSource::notice(Notice::Kind kind, std::string title, std::string body)
{
    Notice n;
    n.kind = kind;
    n.title = std::move(title);
    n.body = std::move(body);
    std::lock_guard<std::mutex> lock(mutex_);
    notices_.push_back(std::move(n));
}

void HttpSource::set_link(Link link, std::string detail)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (data_.link == link && data_.link_detail == detail)
        return;
    data_.link = link;
    data_.link_detail = std::move(detail);
    ++data_.serial;
}

std::string HttpSource::game_name(const std::string &id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Game &g : data_.games)
    {
        if (g.id == id)
            return g.name;
    }
    return id;
}

// Until the app answers: ask it, and when nothing listens send it once to the
// ELF loader, exactly as the old starter tile did. True once it answers.
bool HttpSource::connect_phase()
{
    if (retry_.exchange(false))
    {
        payload_sent_ = false;
        starting_since_us_ = 0;
        set_link(Link::starting, "Verbindung zur App …");
    }
    if (data_.link == Link::failed)
        return false;
    int status = 0;
    std::string body;
    const Net net = exchange("GET", "/api/v1/status", {}, &status, &body, 2);
    if (net == Net::ok && status == 200)
        return true;
    const std::int64_t now = sys::monotonic_us();
    if (starting_since_us_ == 0)
        starting_since_us_ = now;
    if (net == Net::refused && !payload_sent_)
    {
        payload_sent_ = true;
        Net why = Net::failed;
        const int s = open_local(kPortLoader, 5, &why);
        const std::size_t size = static_cast<std::size_t>(ps5cc_payload_elf_end - ps5cc_payload_elf);
        const bool sent = s >= 0 && send_all(s, ps5cc_payload_elf, size);
        if (s >= 0)
            close(s);
        sys::log("[PS5CC] payload %zu bytes sent=%d", size, sent ? 1 : 0);
        if (!sent)
        {
            set_link(Link::failed, "Der ELF-Loader (Port 9021) ist nicht erreichbar.");
            return false;
        }
        set_link(Link::starting, "Die App wird gestartet …");
    }
    if (now - starting_since_us_ > kStartLimitUs)
        set_link(Link::failed, "127.0.0.1:8086 antwortet nach 30 s nicht.");
    return false;
}

void HttpSource::run()
{
    std::int64_t last_status = 0, last_library = 0, last_jobs = 0, last_history = 0, last_cover = 0;
    std::int64_t last_config = 0, last_selftest = 0, last_payloads = 0, last_store = 0, last_log = 0, last_klog = 0;
    bool library_wanted = true;
    for (;;)
    {
        if (!online_)
        {
            if (connect_phase())
            {
                online_ = true;
                status_failures_ = 0;
                set_link(Link::online, {});
                library_wanted = true;
                config_wanted_ = true;
                last_status = last_jobs = last_history = 0;
                last_selftest = last_payloads = last_store = last_log = last_klog = 0;
            }
            else
            {
                sys::sleep_us(500000);
                continue;
            }
        }
        if (retry_.exchange(false) || library_wanted_.exchange(false))
            library_wanted = true;

        for (;;)
        {
            Request request;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (requests_.empty())
                    break;
                request = requests_.front();
                requests_.pop_front();
            }
            if (request.kind == Request::Kind::reload)
                library_wanted = true;
            handle(request);
        }

        const std::int64_t now = sys::monotonic_us();
        if (now - last_status >= 1000000)
        {
            last_status = now;
            if (!poll_status())
            {
                if (++status_failures_ >= 3 && !reported_silence_)
                {
                    reported_silence_ = true;
                    notice(Notice::Kind::error, "Die App antwortet nicht",
                           "Die Werte sind veraltet, bis sie wieder antwortet.");
                }
                // Nothing listens any more: the app ended. Start it again.
                int status = 0;
                std::string body;
                if (exchange("GET", "/api/v1/status", {}, &status, &body, 2) == Net::refused)
                {
                    online_ = false;
                    payload_sent_ = false;
                    starting_since_us_ = 0;
                    set_link(Link::starting, "Die App läuft nicht mehr und wird neu gestartet …");
                    continue;
                }
            }
            else
            {
                status_failures_ = 0;
                if (reported_silence_)
                {
                    reported_silence_ = false;
                    notice(Notice::Kind::success, "Die App antwortet wieder");
                }
            }
        }
        if (library_wanted || now - last_library >= 30000000)
        {
            library_wanted = false;
            last_library = now;
            poll_library();
            covers().forget_failures();
        }
        // One cover per pass (the loop sleeps 100 ms): about 380 KB of PNG each, decoded and shrunk here so the
        // frame only uploads 100 KiB.
        if (now - last_cover >= 250000)
        {
            last_cover = now;
            fetch_cover();
        }
        const bool busy = [&]
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const Job &job : data_.jobs)
            {
                if (job.active)
                    return true;
            }
            return false;
        }();
        const Watch page = watching_.load();
        const bool jobs_open = page == Watch::jobs;
        const std::int64_t jobs_every = jobs_open || busy ? 1000000 : 10000000;
        if (now - last_jobs >= jobs_every || jobs_wanted_.exchange(false))
        {
            last_jobs = now;
            poll_jobs();
        }
        if (now - last_history >= (jobs_open ? 10000000 : 60000000))
        {
            last_history = now;
            poll_history();
        }
        // Each page's data only while it is open (the config also once at the start: the settings page opens filled).
        if (config_wanted_.exchange(false) || (page == Watch::settings && now - last_config >= 15000000))
        {
            last_config = now;
            poll_config();
        }
        if (page == Watch::settings)
        {
            const bool testing = [&]
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return data_.selftest.state == "running" || data_.selftest.state == "requested";
            }();
            if (now - last_selftest >= (testing ? 1000000 : 5000000))
            {
                last_selftest = now;
                poll_selftest();
            }
        }
        if (page == Watch::payloads)
        {
            if (now - last_payloads >= 4000000)
            {
                last_payloads = now;
                poll_payloads();
            }
            const bool storing = [&]
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return data_.store_job.state == "running" || data_.store_fetching || !data_.store_loaded;
            }();
            if (now - last_store >= (storing ? 1500000 : 10000000))
            {
                last_store = now;
                poll_store();
            }
        }
        if (page == Watch::logs)
        {
            if (now - last_log >= 2000000)
            {
                last_log = now;
                poll_log();
            }
            if (now - last_klog >= 1000000)
            {
                last_klog = now;
                poll_klog();
            }
        }
        sys::sleep_us(100000);
    }
}

void HttpSource::handle(const Request &request)
{
    int status = 0;
    std::string body;
    char payload[160];
    switch (request.kind)
    {
    case Request::Kind::target:
    {
        std::snprintf(payload, sizeof(payload), "{\"threshold_c\":%d}", request.value);
        if (exchange("POST", "/api/v1/fan/threshold", payload, &status, &body) == Net::ok && status == 200)
        {
            char title[64];
            std::snprintf(title, sizeof(title), "Zieltemperatur %d °C übernommen", request.value);
            notice(Notice::Kind::success, title);
            poll_status();
        }
        else
        {
            notice(Notice::Kind::error, "Zieltemperatur nicht übernommen",
                   message_of(body, "Die App hat nicht geantwortet."));
        }
        break;
    }
    case Request::Kind::launch:
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "id", request.id.c_str());
        if (!own_title_id().empty())
            cJSON_AddStringToObject(root, "after_exit", own_title_id().c_str());
        char *text = cJSON_PrintUnformatted(root);
        const Net net = exchange("POST", "/api/v1/library/launch", text != nullptr ? text : "{}", &status,
                                 &body, 8);
        cJSON_free(text);
        cJSON_Delete(root);
        if (net != Net::ok)
        {
            notice(Notice::Kind::error, "Starten fehlgeschlagen", "Die App hat nicht geantwortet.");
            break;
        }
        cJSON *reply = cJSON_Parse(body.c_str());
        const std::string code = reply != nullptr ? str(reply, "code") : "";
        if (status == 409 && code == "other_running")
        {
            Notice n;
            n.kind = Notice::Kind::other_running;
            n.title = "Ein anderes Spiel läuft";
            const cJSON *running = cJSON_GetObjectItemCaseSensitive(reply, "running");
            const cJSON *target = cJSON_GetObjectItemCaseSensitive(reply, "target");
            n.running_id = str(running, "id");
            n.running_name = str(running, "name");
            n.target_id = str(target, "id");
            n.target_name = str(target, "name");
            if (n.running_name.empty())
                n.running_name = n.running_id;
            if (n.target_name.empty())
                n.target_name = game_name(request.id);
            std::lock_guard<std::mutex> lock(mutex_);
            notices_.push_back(n);
        }
        else if (status == 202 && str(reply, "state") == "after_exit")
        {
            notice(Notice::Kind::leaving, "„" + game_name(request.id) + "“ wird gestartet",
                   "Die Vollbild-App schließt sich dafür.");
        }
        else if (status >= 200 && status < 300)
        {
            notice(Notice::Kind::success, "„" + game_name(request.id) + "“ wird gestartet");
        }
        else
        {
            notice(Notice::Kind::error, "Starten nicht möglich", message_of(body, "Unbekannter Fehler."));
        }
        cJSON_Delete(reply);
        break;
    }
    case Request::Kind::close:
    {
        std::snprintf(payload, sizeof(payload), "{\"id\":\"%.20s\"}", request.id.c_str());
        // On success the screen has said it already ("… wird beendet, danach startet …"); only a refusal is news.
        if (exchange("POST", "/api/v1/library/close", payload, &status, &body) != Net::ok || status >= 300)
            notice(Notice::Kind::error, "Beenden nicht möglich", message_of(body, "Unbekannter Fehler."));
        break;
    }
    case Request::Kind::cancel:
    {
        // A delete has no way back once it started: the screen never asks to cancel one.
        static const char *const kRoutes[] = {"/api/v1/library/copy/cancel", "/api/v1/library/convert/cancel",
                                              "/api/v1/library/image/cancel", "/api/v1/library/storage/cancel"};
        const int index = std::clamp(request.value, 0, 3);
        if (exchange("POST", kRoutes[index], "{}", &status, &body) == Net::ok && status < 300)
            notice(Notice::Kind::info, "Auftrag wird abgebrochen");
        else
            notice(Notice::Kind::error, "Abbrechen nicht möglich", message_of(body, "Unbekannter Fehler."));
        poll_jobs();
        break;
    }
    case Request::Kind::web:
        if (exchange("POST", "/api/v1/open-ui", "{}", &status, &body) == Net::ok && status < 300)
            notice(Notice::Kind::info, "Weboberfläche", "Wird im Browser der Konsole geöffnet.");
        else
            notice(Notice::Kind::error, "Browser nicht geöffnet", message_of(body, "Unbekannter Fehler."));
        break;
    case Request::Kind::reload:
        notice(Notice::Kind::info, "Daten werden neu geladen");
        break;
    case Request::Kind::config:
        if (exchange("PUT", "/api/v1/config", request.text, &status, &body) == Net::ok && status < 300)
            notice(Notice::Kind::success, "Gespeichert");
        else
            notice(Notice::Kind::error, "Nicht gespeichert", message_of(body, "Die App hat nicht geantwortet."));
        poll_config();
        poll_status();
        break;
    case Request::Kind::selftest:
        if (exchange("POST", "/api/v1/fan/selftest", request.value != 0 ? "{\"action\":\"start\"}" : "{\"action\":\"abort\"}",
                     &status, &body) != Net::ok ||
            status >= 300)
            notice(Notice::Kind::error, request.value != 0 ? "Selbsttest nicht möglich" : "Abbrechen nicht möglich",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_selftest();
        break;
    case Request::Kind::power:
    {
        std::snprintf(payload, sizeof(payload), "{\"action\":\"%.16s\"}", request.id.c_str());
        if (exchange("POST", "/api/v1/power", payload, &status, &body) == Net::ok && status < 300)
            notice(Notice::Kind::info, "Befehl angenommen", message_of(body, ""));
        else
            notice(Notice::Kind::error, "Nicht ausgeführt", message_of(body, "Die App hat nicht geantwortet."));
        break;
    }
    case Request::Kind::payload_start:
        if (exchange("POST", "/api/v1/payload-files/start", request.text, &status, &body, 10) == Net::ok &&
            status < 300)
            notice(Notice::Kind::success, "„" + request.id + "“ gestartet", "An den ELF-Loader gesendet.");
        else
            notice(Notice::Kind::error, "„" + request.id + "“ nicht gestartet",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_payloads();
        break;
    case Request::Kind::payload_kill:
        std::snprintf(payload, sizeof(payload), "{\"pid\":%d}", request.value);
        if (exchange("POST", "/api/v1/payloads/kill", payload, &status, &body) == Net::ok && status < 300)
            notice(Notice::Kind::info, "Payload beendet");
        else
            notice(Notice::Kind::error, "Beenden nicht möglich", message_of(body, "Die App hat nicht geantwortet."));
        poll_payloads();
        break;
    case Request::Kind::store:
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "action", request.text.c_str());
        if (!request.id.empty())
            cJSON_AddStringToObject(root, "id", request.id.c_str());
        char *text = cJSON_PrintUnformatted(root);
        const Net net = exchange("POST", "/api/v1/paystore", text != nullptr ? text : "{}", &status, &body, 10);
        cJSON_free(text);
        cJSON_Delete(root);
        if (net != Net::ok || status >= 300)
            notice(Notice::Kind::error, "Payload-Store", message_of(body, "Die App hat nicht geantwortet."));
        else if (request.text == "start")
            notice(Notice::Kind::success, "Gestartet", "An den ELF-Loader gesendet.");
        else if (request.text == "refresh")
            notice(Notice::Kind::info, "Liste wird neu geladen");
        poll_store();
        break;
    }
    }
}

void HttpSource::poll_config()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/config", {}, &status, &body) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    Config c;
    c.valid = true;
    c.mode = str(root, "mode");
    c.profile = str(root, "profile");
    c.target_min_c = static_cast<int>(num(root, "target_min_c", 50));
    c.target_max_c = static_cast<int>(num(root, "target_max_c", 91));
    c.safety_temp_c = static_cast<int>(num(root, "safety_temp_c", 78));
    c.safety_min_c = static_cast<int>(num(root, "safety_min_c", 72));
    c.safety_max_c = static_cast<int>(num(root, "safety_max_c", 95));
    c.night_enabled = num(root, "night_enabled", 0) != 0.0;
    c.night_start_min = static_cast<int>(num(root, "night_start_min", 1380));
    c.night_end_min = static_cast<int>(num(root, "night_end_min", 420));
    c.night_target_c = static_cast<int>(num(root, "night_target_c", 75));
    c.tz_offset_min = static_cast<int>(num(root, "night_tz_offset_min", 0));
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.config = c;
    ++data_.serial;
}

void HttpSource::poll_selftest()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/fan/selftest", {}, &status, &body) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    SelfTest t;
    t.state = str(root, "state");
    t.verdict = str(root, "verdict");
    t.why = str(root, "why");
    t.baseline_pct = static_cast<int>(num(root, "baseline_pct"));
    t.peak_pct = static_cast<int>(num(root, "peak_pct"));
    t.rise_pct = static_cast<int>(num(root, "rise_pct"));
    t.threshold_c = static_cast<int>(num(root, "threshold_c"));
    t.hottest_c = static_cast<int>(num(root, "hottest_c"));
    t.elapsed_s = static_cast<int>(num(root, "elapsed_s"));
    t.limit_s = static_cast<int>(num(root, "limit_s"));
    const cJSON *sample = nullptr;
    cJSON_ArrayForEach(sample, cJSON_GetObjectItemCaseSensitive(root, "samples"))
    {
        if (cJSON_IsNumber(sample))
            t.samples.push_back(static_cast<int>(sample->valuedouble));
    }
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.selftest = t;
    ++data_.serial;
}

void HttpSource::poll_payloads()
{
    int status = 0;
    std::string body;
    std::vector<PayloadFile> files;
    bool files_ok = false;
    if (exchange("GET", "/api/v1/payload-files", {}, &status, &body, 6) == Net::ok && status == 200)
    {
        if (cJSON *root = cJSON_Parse(body.c_str()))
        {
            files_ok = true;
            const auto add = [&](const cJSON *item, const char *source, const std::string &mount,
                                 const std::string &where)
            {
                PayloadFile f;
                f.source = source;
                f.mount = mount;
                f.where = where;
                f.dir = str(item, "dir");
                f.name = str(item, "name");
                f.size = static_cast<std::uint64_t>(num(item, "size"));
                f.mtime = static_cast<std::int64_t>(num(item, "mtime"));
                const cJSON *valid = cJSON_GetObjectItemCaseSensitive(item, "valid");
                f.valid = !cJSON_IsBool(valid) || cJSON_IsTrue(valid);
                f.why = str(item, "why");
                if (f.why.empty())
                    f.why = str(item, "reason");
                files.push_back(std::move(f));
            };
            const cJSON *item = nullptr;
            cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "internal"))
                add(item, "internal", {}, "Intern");
            const cJSON *usb = nullptr;
            cJSON_ArrayForEach(usb, cJSON_GetObjectItemCaseSensitive(root, "usb"))
            {
                const std::string mount = str(usb, "mount");
                const std::string label = str(usb, "label");
                cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(usb, "items"))
                    add(item, "usb", mount, label.empty() ? mount : label);
            }
            cJSON_Delete(root);
        }
    }
    std::vector<RunningPayload> running;
    bool running_ok = false;
    if (exchange("GET", "/api/v1/payloads", {}, &status, &body, 6) == Net::ok && status == 200)
    {
        if (cJSON *root = cJSON_Parse(body.c_str()))
        {
            running_ok = true;
            const cJSON *item = nullptr;
            cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "payloads"))
            {
                RunningPayload p;
                p.pid = static_cast<int>(num(item, "pid"));
                p.name = str(item, "name");
                p.memory_mb = static_cast<int>(num(item, "memory_mb"));
                p.self = flag(item, "is_self");
                running.push_back(std::move(p));
            }
            cJSON_Delete(root);
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (files_ok)
    {
        data_.payload_files = std::move(files);
        data_.payload_files_loaded = true;
    }
    if (running_ok)
        data_.running = std::move(running);
    if (files_ok || running_ok)
        ++data_.serial;
}

void HttpSource::poll_store()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/paystore?refresh=1", {}, &status, &body, 15) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    std::vector<StoreItem> items;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "items"))
    {
        StoreItem s;
        s.id = str(item, "id");
        s.name = str(item, "name");
        s.filename = str(item, "filename");
        s.version = str(item, "version");
        s.category = str(item, "category");
        s.description = str(item, "description");
        s.installed_version = str(cJSON_GetObjectItemCaseSensitive(item, "installed"), "version");
        s.update = flag(item, "update");
        s.name_taken = flag(item, "name_taken");
        s.owner = str(item, "owner");
        s.avatar = flag(item, "avatar");
        items.push_back(std::move(s));
    }
    StoreJob job;
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(root, "job");
    job.state = str(j, "state");
    job.id = str(j, "id");
    job.name = str(j, "name");
    job.error = str(j, "error");
    job.done = num(j, "done");
    job.total = num(j, "total");
    const bool loaded = flag(root, "loaded");
    const bool fetching = flag(root, "fetching");
    const std::string error = str(root, "error");
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    if (data_.store_job.state == "running" && job.state == "done")
        notices_.push_back(
            {Notice::Kind::success, "„" + job.name + "“ geladen", {}, {}, {}, {}, {}, JobKind::count});
    if (data_.store_job.state == "running" && job.state == "failed")
        notices_.push_back(
            {Notice::Kind::error, "„" + job.name + "“ nicht geladen", job.error, {}, {}, {}, {}, JobKind::count});
    data_.store = std::move(items);
    data_.store_job = job;
    data_.store_loaded = loaded;
    data_.store_fetching = fetching;
    data_.store_error = error;
    ++data_.serial;
}

void HttpSource::poll_log()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/logs/tail?count=150", {}, &status, &body) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    std::vector<LogEntry> log;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "entries"))
    {
        LogEntry e;
        e.t_ms = static_cast<std::int64_t>(num(item, "timestamp_ms"));
        e.level = str(item, "level");
        e.code = str(item, "code");
        e.message = str(item, "message");
        log.push_back(std::move(e));
    }
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.log = std::move(log);
    ++data_.serial;
}

void HttpSource::poll_klog()
{
    std::int64_t after = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!data_.klog.empty())
            after = data_.klog.back().n;
    }
    char path[96];
    std::snprintf(path, sizeof(path), "/api/v1/klog?after=%lld&max=%d", static_cast<long long>(after),
                  after == 0 ? 300 : 500);
    int status = 0;
    std::string body;
    if (exchange("GET", path, {}, &status, &body) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    std::vector<KlogLine> fresh;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "lines"))
    {
        KlogLine line;
        line.n = static_cast<std::int64_t>(num(item, "n"));
        line.t_ms = static_cast<std::int64_t>(num(item, "t"));
        line.text = str(item, "s");
        fresh.push_back(std::move(line));
    }
    const bool lost = flag(root, "lost");
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    const bool added = klog_append(data_.klog, fresh, kKlogKeep);
    if (added || lost != data_.klog_lost)
    {
        data_.klog_lost = data_.klog_lost || lost;
        ++data_.serial;
    }
}

bool HttpSource::poll_status()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/status", {}, &status, &body) != Net::ok || status != 200)
        return false;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return false;
    Status s;
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "temperatures");
    const cJSON *fan = cJSON_GetObjectItemCaseSensitive(root, "fan");
    const cJSON *control = cJSON_GetObjectItemCaseSensitive(root, "control");
    const cJSON *game = cJSON_GetObjectItemCaseSensitive(root, "game");
    s.valid = true;
    s.cpu_c = flag(t, "cpu_valid") ? static_cast<int>(num(t, "cpu_c", -1)) : -1;
    s.soc_c = flag(t, "soc_valid") ? static_cast<int>(num(t, "soc_c", -1)) : -1;
    s.gpu_c = flag(t, "gpu_valid") ? static_cast<int>(num(t, "gpu_c", -1)) : -1;
    s.hottest_c = static_cast<int>(num(t, "hottest_c", -1));
    if (flag(fan, "measured_valid"))
        s.fan_pct = static_cast<float>(num(fan, "measured_duty_pct10", -10) / 10.0);
    s.automatic = flag(fan, "automatic");
    s.safety = flag(control, "safety_active");
    s.guard = static_cast<int>(num(control, "guard_state", 0));
    s.night = flag(control, "night_active");
    s.target_c = static_cast<int>(num(control, "effective_target_c", num(control, "target_temp_c", 0)));
    s.game_state = str(game, "state");
    s.game_name = str(game, "title_name");
    s.game_id = str(game, "title_id");
    s.game_foreground = flag(game, "foreground");
    if (!s.game_id.empty() && s.game_id == own_title_id())
    {
        // This app is a title too: the console app sees it as the game in front.
        s.game_state.clear();
        s.game_name.clear();
        s.game_id.clear();
        s.game_foreground = false;
    }
    cJSON_Delete(root);

    std::lock_guard<std::mutex> lock(mutex_);
    data_.status = s;
    ++data_.serial;
    return true;
}

void HttpSource::poll_library()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/library", {}, &status, &body, 20) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    std::vector<Game> games;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "games"))
    {
        Game g;
        g.id = str(item, "title_id");
        // The app's two tiles (and their ids before 1.74.1) are titles of their own, not games to start from here.
        if (g.id == own_title_id() || g.id == "FAKE86921" || g.id == "FAKE86922" || g.id == "PPSA86921" ||
            g.id == "PPSA86922")
            continue;
        g.name = str(item, "name");
        g.platform = str(item, "platform");
        g.version = str(item, "version");
        g.format = str(item, "format");
        g.path = str(item, "path");
        g.cover = str(item, "cover");
        // Only the app's own route: the bytes are fetched from 127.0.0.1 and must not lead anywhere else.
        if (g.cover.rfind("/api/v1/library/cover?", 0) != 0)
            g.cover.clear();
        g.last_played = str(item, "last_played");
        g.size_bytes = static_cast<std::uint64_t>(num(item, "size_bytes"));
        g.played_s = static_cast<std::uint64_t>(num(item, "played_s"));
        const cJSON *can = cJSON_GetObjectItemCaseSensitive(item, "can_launch");
        g.can_launch = !cJSON_IsBool(can) || cJSON_IsTrue(can);
        g.can_copy = flag(item, "can_copy");
        g.can_convert = flag(item, "can_convert");
        g.can_move = flag(item, "can_move");
        g.can_unpack = flag(item, "can_unpack");
        g.ampr_emu = flag(cJSON_GetObjectItemCaseSensitive(item, "mods"), "ampr_emu");
        if (g.name.empty())
            g.name = g.id;
        games.push_back(std::move(g));
    }
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.games = std::move(games);
    data_.games_loaded = true;
    ++data_.serial;
}

void HttpSource::fetch_cover()
{
    // Game covers first, then the developer pictures the console app has cached, under keys of their own.
    std::vector<Game> games;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (data_.games_loaded)
            games = data_.games;
        for (const StoreItem &s : data_.store)
        {
            if (!s.avatar || s.owner.empty())
                continue;
            const std::string key = avatar_key(s.owner);
            if (std::any_of(games.begin(), games.end(), [&](const Game &g) { return g.id == key; }))
                continue;
            Game g;
            g.id = key;
            g.cover = "/api/v1/paystore/avatar?owner=" + s.owner;
            games.push_back(std::move(g));
        }
    }
    if (games.empty())
        return;
    const CoverWant want = covers().next_wanted(games);
    if (want.path.empty())
        return;
    int status = 0;
    std::string body;
    if (exchange("GET", want.path.c_str(), {}, &status, &body, 8) != Net::ok || status != 200)
    {
        covers().fail(want.id);
        return;
    }
    std::vector<std::uint8_t> rgba =
        decode_cover(reinterpret_cast<const std::uint8_t *>(body.data()), body.size());
    if (rgba.empty())
        covers().fail(want.id);
    else
        covers().deliver(want.id, std::move(rgba));
}

void HttpSource::poll_jobs()
{
    static const char *const kRoutes[] = {"/api/v1/library/copy", "/api/v1/library/convert",
                                          "/api/v1/library/image/job", "/api/v1/library/storage",
                                          "/api/v1/library/delete"};
    std::array<Job, static_cast<std::size_t>(JobKind::count)> jobs{};
    for (std::size_t i = 0; i < jobs.size(); ++i)
    {
        int status = 0;
        std::string body;
        // Without ShadowMountPlus its route answers 503: the card simply stays empty.
        if (exchange("GET", kRoutes[i], {}, &status, &body) != Net::ok || status != 200)
            continue;
        cJSON *root = cJSON_Parse(body.c_str());
        if (root == nullptr)
            continue;
        Job &job = jobs[i];
        if (i == static_cast<std::size_t>(JobKind::storage))
        {
            read_storage_job(root, job);
            cJSON_Delete(root);
            continue;
        }
        if (i == static_cast<std::size_t>(JobKind::deletion))
        {
            read_delete_job(root, job);
            cJSON_Delete(root);
            continue;
        }
        job.state = str(root, "state");
        job.error = str(root, "error");
        const bool idle = job.state.empty() || job.state == "idle";
        job.active = !idle && (flag(root, "active") || job.state == "running" || job.state == "scanning" ||
                               job.state == "verifying");
        job.finished = !idle && !job.active;
        job.name = str(root, "name");
        if (job.name.empty())
            job.name = str(root, "title_id");
        if (i == 0)
        {
            // Written and read back count together, as in the web app.
            job.total = 2.0 * num(root, "total_bytes");
            job.done = num(root, "done_bytes") + num(root, "checked_bytes");
            const std::string drive = str(root, "drive");
            job.detail = drive.empty() ? "" : "→ " + drive;
        }
        else if (i == 1)
        {
            const bool checking = job.state == "verifying";
            job.total = num(root, checking ? "check_total_bytes" : "total_bytes");
            job.done = num(root, checking ? "check_done_bytes" : "done_bytes");
            const std::string target = str(root, "target");
            job.detail = target.empty() ? str(root, "op") : "→ " + target;
        }
        else
        {
            job.total = num(root, "work_total");
            job.done = num(root, "work_done");
            const std::string kind = str(root, "kind");
            if (job.name.empty())
                job.name = kind == "check" ? "Prüfen" : kind == "unpack" ? "Entpacken" : kind == "repair" ? "Reparieren" : kind;
            job.detail = str(root, "current");
            job.active = job.state == "running";
            job.finished = !idle && !job.active;
        }
        cJSON_Delete(root);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    // A job that just ended may have moved, unpacked or deleted a game: the list is read again.
    for (std::size_t i = 0; i < jobs.size(); ++i)
    {
        if (data_.jobs[i].active && !jobs[i].active)
            library_wanted_ = true;
    }
    data_.jobs = jobs;
    ++data_.serial;
}

void HttpSource::read_storage_job(const cJSON *root, Job &job)
{
    const std::int64_t id = static_cast<std::int64_t>(num(root, "job_id"));
    const bool active = flag(root, "active");
    if (active && id > 0)
        storage_seen_ = id;
    if (!active && (id <= 0 || id != storage_seen_.load()))
        return; // an old job of ShadowMountPlus, perhaps from its own interface
    const std::string state = str(root, "state");
    job.active = active;
    job.finished = !active;
    job.state = active                  ? state
                : state == "completed"  ? "done"
                : state == "failed"     ? "failed"
                                        : "cancelled";
    std::string tid = str(root, "title_id");
    job.name = !tid.empty() ? game_name(tid) : str(root, "title_name");
    if (job.name.empty())
        job.name = str(root, "name");
    const std::string op = str(root, "operation");
    const std::string verb = op == "move" ? "Verschieben" : op == "unpack" ? "Entpacken" : op == "install" ? "Installieren" : op;
    const std::string dest = str(root, "destination");
    job.detail = verb + (dest.empty() ? "" : " → " + dest);
    job.total = num(root, "total_bytes");
    job.done = num(root, "processed_bytes");
    job.error = str(root, "result_error");
    job.cancellable = flag(root, "cancellable") && !flag(root, "cancel_requested");
}

void HttpSource::read_delete_job(const cJSON *root, Job &job)
{
    job.state = str(root, "state");
    if (job.state.empty() || job.state == "idle")
        return;
    job.active = flag(root, "active");
    // The app keeps the last delete until it restarts; an hour later it is no news any more.
    const double ago = num(root, "finished_ago_s", -1.0);
    if (!job.active && (ago < 0.0 || ago > 3600.0))
    {
        job = Job{};
        return;
    }
    job.finished = !job.active;
    job.name = str(root, "name");
    job.detail = job.active ? str(root, "phase") : str(root, "note");
    job.total = num(root, "bytes_total");
    job.done = num(root, "bytes_done");
    job.error = str(root, "error");
    job.cancellable = false;
}

void HttpSource::poll_history()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/jobs/history", {}, &status, &body, 6) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    std::vector<HistoryEntry> history;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "entries"))
    {
        if (history.size() >= 20)
            break;
        HistoryEntry e;
        e.t = static_cast<std::int64_t>(num(item, "t"));
        e.kind = str(item, "kind");
        e.title = str(item, "title");
        e.detail = str(item, "detail");
        e.result = str(item, "result");
        e.bytes = static_cast<std::uint64_t>(num(item, "bytes"));
        history.push_back(std::move(e));
    }
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.history = std::move(history);
    ++data_.serial;
}

} // namespace hui::ps5cc
