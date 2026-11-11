// ps5cc - The console app's data over HTTP (127.0.0.1:8086), on a thread of its own.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plain HTTP/1.1 with "Connection: close", one request at a time, from one
// worker thread: console threads run first-in first-out and are never
// time-sliced, so nothing here may run on the frame. Every call of the screen
// only queues a request or copies the last data.

#include "platform/ps5/http_source.hpp"

#include "core/save_file.hpp"
#include "platform/ps5/system.hpp"
#include "ps5cc/avatar_build.hpp"
#include "ps5cc/covers.hpp"
#include "ps5cc/credits.hpp"
#include "ps5cc/i18n.hpp"
#include "third_party/cjson/cJSON.h"

#include <algorithm>
#include <array>
#include <memory>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <netinet/in.h>
#include "ps5cc/app_update.hpp"

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
    static const std::string id = []
    {
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
        const cJSON *tid =
            root != nullptr ? cJSON_GetObjectItemCaseSensitive(root, "titleId") : nullptr;
        if (tid != nullptr && cJSON_IsString(tid) && tid->valuestring != nullptr)
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

// A title id for a query string: letters, digits, '-' and '_' as they are, everything else
// percent-encoded.
// A file name inside a path (/avatars/<file>): as url_encoded, but the dot stays, since the
// console app serves its files by the name as it is and does not decode the path.
std::string path_encoded(const std::string &text);

std::string url_encoded(const std::string &text)
{
    std::string out;
    for (const char c : text)
    {
        const auto u = static_cast<unsigned char>(c);
        if ((u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
            u == '-' || u == '_')
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

std::string path_encoded(const std::string &text)
{
    std::string out;
    std::size_t from = 0;
    for (std::size_t dot = text.find('.'); dot != std::string::npos; dot = text.find('.', from))
    {
        out += url_encoded(text.substr(from, dot - from)) + ".";
        from = dot + 1;
    }
    return out + url_encoded(text.substr(from));
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
void HttpSource::remount_game(const std::string &id, const std::string &action)
{
    push({Request::Kind::remount, 0, id, action});
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
void HttpSource::set_backdrop(const std::string &area, BackdropMode mode)
{
    if (mode == BackdropMode::custom)
    {
        bool have = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const BackdropSetting &b : data_.backdrops)
                have = have || (b.id == area && b.custom);
            if (!have)
                ++data_.serial; // the page shows the setting as it was again
        }
        if (!have)
        {
            notice(
                Notice::Kind::error, "Kein eigenes Bild",
                "Öffne zuerst die Browser-App aus dieser App (Options → Weboberfläche im Browser öffnen) und lade es dort hoch: System → Hintergründe der Vollbild-App.");
            return;
        }
    }
    const char *const names[] = {"standard", "custom", "off"};
    push({Request::Kind::backdrop, 0, area, names[static_cast<int>(mode)]});
}
void HttpSource::package_action(const std::string &action, const std::string &id)
{
    push({Request::Kind::package, 0, id, action});
}
void HttpSource::split_package(const std::string &id, const std::string &target, int part_mb)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", id.c_str());
    cJSON_AddStringToObject(o, "target", target.c_str());
    cJSON_AddNumberToObject(o, "part_mb", part_mb);
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (text == nullptr)
        return;
    push({Request::Kind::package, 0, text, "split"});
    cJSON_free(text);
}
void HttpSource::ps4mount_post(const std::string &route, const std::string &json)
{
    push({Request::Kind::ps4mount, 0, route, json});
}
void HttpSource::ps4mount_fetch(const std::string &what, const std::string &id)
{
    push({Request::Kind::ps4mount_fetch, 0, id, what});
}
void HttpSource::disc_post(const std::string &route, const std::string &json)
{
    push({Request::Kind::disc, 0, route, json});
}
void HttpSource::usb_eject(const std::string &mount)
{
    push({Request::Kind::eject, 0, mount, {}});
}
void HttpSource::playtime_reset()
{
    push({Request::Kind::playtime_reset, 0, {}, {}});
}
void HttpSource::saves_post(const std::string &route, const std::string &json)
{
    push({Request::Kind::saves, 0, route, json});
}
void HttpSource::backup_delete(const std::string &path)
{
    push({Request::Kind::backup_delete, 0, path, {}});
}
void HttpSource::extras_open(const std::string &id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    extras_id_ = id;
}
void HttpSource::extras_post(const std::string &route, const std::string &json)
{
    push({Request::Kind::extras_post, 0, route, json});
}
void HttpSource::extras_versions(const std::string &lib)
{
    push({Request::Kind::extras_versions, 0, lib, {}});
}
void HttpSource::extras_lz4_plan(const std::string &id, int level, const std::string &profile,
                                 const std::string &format)
{
    push({Request::Kind::extras_plan, level, id, profile, format});
}
void HttpSource::library_tools_fetch()
{
    push({Request::Kind::lib_tools, 0, {}, {}});
}
void HttpSource::library_tools_post(const std::string &route, const std::string &json)
{
    push({Request::Kind::lib_tools_post, 0, route, json});
}
void HttpSource::system_post(const std::string &route, const std::string &json)
{
    push({Request::Kind::sys_post, 0, route, json});
}
void HttpSource::risky_measure()
{
    push({Request::Kind::risky, 0, {}, {}});
}
void HttpSource::klog_record(bool on)
{
    push({Request::Kind::klog_record, on ? 1 : 0, {}, {}});
}
void HttpSource::set_username(const std::string &name)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", name.c_str());
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (text == nullptr)
        return;
    push({Request::Kind::profile_name, 0, name, text});
    cJSON_free(text);
}
void HttpSource::apply_avatar(const std::string &file)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.avatar_busy = true;
        ++data_.serial;
    }
    push({Request::Kind::avatar_apply, 0, file, {}});
}
void HttpSource::apply_own_avatar(const std::string &path)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.avatar_busy = true;
        ++data_.serial;
    }
    push({Request::Kind::avatar_apply, 1, path, {}});
}
void HttpSource::restore_avatar()
{
    push({Request::Kind::avatar_restore, 0, {}, "{}"});
}
void HttpSource::account_action(const std::string &action, const std::string &name)
{
    std::string json = "{}";
    if (action == "activate")
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "username", name.c_str());
        char *text = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        if (text == nullptr)
            return;
        json = text;
        cJSON_free(text);
    }
    push({Request::Kind::account_post, 0, action, json});
}
void HttpSource::klog_save(const std::string &text)
{
    int tz = 0; // the file is named after the local time, as the web page's tz
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tz = data_.config.tz_offset_min;
    }
    push({Request::Kind::klog_save, tz, {}, text});
}
void HttpSource::klog_files_clear()
{
    push({Request::Kind::klog_clear, 0, {}, "{}"});
}
void HttpSource::files_open(const std::string &path)
{
    push({Request::Kind::fs_open, 0, path, {}});
}
void HttpSource::files_post(const std::string &route, const std::string &json)
{
    push({Request::Kind::fs_post, 0, route, json});
}
void HttpSource::files_conflict(const std::string &choice)
{
    std::string route;
    std::string json;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        route = std::move(pending_conflict_route_);
        json = std::move(pending_conflict_json_);
        pending_conflict_route_.clear();
        pending_conflict_json_.clear();
        data_.fs_conflict.names.clear();
        ++data_.fs_conflict.seq;
        ++data_.serial;
    }
    // the same request again, with the choice added before its closing brace
    const std::size_t close = json.find_last_of('}');
    if (choice.empty() || route.empty() || close == std::string::npos)
        return;
    json.insert(close, ",\"conflict\":\"" + choice + "\"");
    push({Request::Kind::fs_post, 0, route, json});
}
void HttpSource::files_delete(const std::vector<std::string> &paths)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "paths");
    for (const std::string &p : paths)
        cJSON_AddItemToArray(list, cJSON_CreateString(p.c_str()));
    char *text = cJSON_PrintUnformatted(root);
    push({Request::Kind::fs_delete, 0, {}, text != nullptr ? text : "{}"});
    cJSON_free(text);
    cJSON_Delete(root);
}
void HttpSource::files_view(const std::string &path)
{
    push({Request::Kind::fs_view, 0, path, {}});
}
void HttpSource::files_size(const std::string &path)
{
    push({Request::Kind::fs_size, 0, path, {}});
}
void HttpSource::watch(Watch page)
{
    watching_ = page;
}
void HttpSource::save_config(const std::string &patch)
{
    push({Request::Kind::config, 0, {}, patch});
}
void HttpSource::save_game_rule(const GameRule &rule)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "title_id", rule.title_id.c_str());
    cJSON_AddStringToObject(o, "title_name", rule.title_name.c_str());
    cJSON_AddNumberToObject(o, "target_temp_c", rule.target_c);
    cJSON_AddStringToObject(o, "profile", rule.profile.c_str());
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (text == nullptr)
        return;
    push({Request::Kind::rule, 1, rule.title_name, text});
    cJSON_free(text);
}
void HttpSource::remove_game_rule(const std::string &title_id)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "title_id", title_id.c_str());
    cJSON_AddBoolToObject(o, "remove", 1);
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (text == nullptr)
        return;
    push({Request::Kind::rule, 0, title_id, text});
    cJSON_free(text);
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
void HttpSource::copy_payload_file(const PayloadFile &file)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "source", "usb");
    cJSON_AddStringToObject(o, "mount", file.mount.c_str());
    cJSON_AddStringToObject(o, "dir", file.dir.c_str());
    cJSON_AddStringToObject(o, "name", file.name.c_str());
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (text == nullptr)
        return;
    push({Request::Kind::payload_post, 0, file.name, text});
    cJSON_free(text);
}
void HttpSource::delete_payload_file(const PayloadFile &file)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", file.name.c_str());
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (text == nullptr)
        return;
    push({Request::Kind::payload_post, 1, file.name, text});
    cJSON_free(text);
}
void HttpSource::save_payload_profiles(const std::vector<PayloadProfile> &profiles,
                                       const std::string &startup)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "startup", startup.c_str());
    cJSON *list = cJSON_AddArrayToObject(o, "profiles");
    for (const PayloadProfile &p : profiles)
    {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "id", p.id.c_str());
        cJSON_AddStringToObject(e, "name", p.name.c_str());
        cJSON *items = cJSON_AddArrayToObject(e, "items");
        for (const std::string &item : p.items)
            cJSON_AddItemToArray(items, cJSON_CreateString(item.c_str()));
        cJSON_AddItemToArray(list, e);
    }
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (text == nullptr)
        return;
    push({Request::Kind::payload_post, 2, {}, text});
    cJSON_free(text);
}
void HttpSource::run_payload_profile(const std::string &id)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", id.c_str());
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (text == nullptr)
        return;
    push({Request::Kind::payload_post, 3, id, text});
    cJSON_free(text);
}
void HttpSource::stop_payload_profile()
{
    push({Request::Kind::payload_post, 4, {}, "{}"});
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
            path = "/api/v1/library/storage/plan?id=" + id +
                   "&op=" + (request.tool == Tool::move ? "move" : "unpack");
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
        // Busy, other_busy, a running game, a target that is gone: the app's own words (409 and the
        // rest).
        notice(Notice::Kind::error, label + " nicht gestartet",
               net == Net::ok ? message_of(body, "Unbekannter Fehler.")
                              : "Die App hat nicht geantwortet.");
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
// The ELF of the app this tile carries to the payload loader (127.0.0.1:9021).
bool HttpSource::send_payload()
{
    Net why = Net::failed;
    const int s = open_local(kPortLoader, 5, &why);
    // Two linker labels around one blob: compare addresses, not pointers into different arrays.
    const std::size_t size = reinterpret_cast<std::uintptr_t>(ps5cc_payload_elf_end) -
                             reinterpret_cast<std::uintptr_t>(ps5cc_payload_elf);
    const bool sent = s >= 0 && send_all(s, ps5cc_payload_elf, size);
    if (s >= 0)
        close(s);
    sys::log("[PS5CC] payload %zu bytes sent=%d", size, sent ? 1 : 0);
    return sent;
}

// An app answers. When it is OLDER than the one this tile carries and no job runs, the tile's own goes over it (the new app
// retires the old one when it starts); a job in progress would end with it, so then the old app stays and the person is told.
bool HttpSource::replace_older_app()
{
    const std::size_t size = reinterpret_cast<std::uintptr_t>(ps5cc_payload_elf_end) -
                             reinterpret_cast<std::uintptr_t>(ps5cc_payload_elf);
    const std::string mine = embedded_version(ps5cc_payload_elf, size);
    if (mine.empty())
        return false;
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/system", {}, &status, &body, 5) != Net::ok || status != 200)
        return false;
    std::string running;
    if (cJSON *root = cJSON_Parse(body.c_str()))
    {
        running = str(root, "app_version");
        cJSON_Delete(root);
    }
    if (!should_replace(running, mine))
        return false;
    // One route for all jobs of the app (since 1.79.x). An answer that does not come, or is not understood, counts as "a job may
    // run": the old app is then left alone (audit 11.11.2026, F-02). Only an app too old for the route (404) is asked job by job,
    // and there too a missing answer counts as busy.
    const Net busy_net = exchange("GET", "/api/v1/jobs/active", {}, &status, &body, 4);
    bool busy = true;
    if (busy_net == Net::ok && status == 200)
        busy = job_active(body);
    else if (busy_net == Net::ok && status == 404)
    {
        static const char *const kJobs[] = {"/api/v1/library/image/job", "/api/v1/files/job",
                                            "/api/v1/library/ampr/job", "/api/v1/library/copy",
                                            "/api/v1/library/convert", "/api/v1/library/delete",
                                            "/api/v1/packages/install/job", "/api/v1/saves/job"};
        busy = false;
        for (const char *route : kJobs)
        {
            const Net n = exchange("GET", route, {}, &status, &body, 3);
            if (n != Net::ok || (status == 200 && job_active(body)))
            {
                busy = true;
                break;
            }
        }
    }
    if (busy)
    {
        notice(Notice::Kind::info, "Eine neuere App liegt bereit",
               "Die App ist gerade beschäftigt. Danach die Kachel noch einmal öffnen.");
        return false;
    }
    set_link(Link::starting, "Die App wird aktualisiert …");
    if (!send_payload())
        return false;
    return true;
}

bool HttpSource::connect_phase()
{
    if (retry_.exchange(false))
    {
        payload_sent_ = false;
        update_checked_ = false;
        awaiting_update_ = false;
        starting_since_us_ = 0;
        set_link(Link::starting, "Verbindung zur App …");
    }
    if (data_.link == Link::failed)
        return false;
    int status = 0;
    std::string body;
    const Net net = exchange("GET", "/api/v1/status", {}, &status, &body, 2);
    if (net == Net::ok && status == 200)
    {
        const std::int64_t at = sys::monotonic_us();
        if (!update_checked_)
        {
            update_checked_ = true;
            if (replace_older_app())
            {
                payload_sent_ = true;
                awaiting_update_ = true;
                starting_since_us_ = at;
                return false;
            }
        }
        if (awaiting_update_)
        {
            // The old app may still answer for a moment; wait for the new one (by its version), but not for ever.
            std::string running;
            if (exchange("GET", "/api/v1/system", {}, &status, &body, 3) == Net::ok && status == 200)
            {
                if (cJSON *root = cJSON_Parse(body.c_str()))
                {
                    running = str(root, "app_version");
                    cJSON_Delete(root);
                }
            }
            const std::size_t size = reinterpret_cast<std::uintptr_t>(ps5cc_payload_elf_end) -
                                     reinterpret_cast<std::uintptr_t>(ps5cc_payload_elf);
            if (running != embedded_version(ps5cc_payload_elf, size) &&
                at - starting_since_us_ <= kStartLimitUs)
                return false;
            awaiting_update_ = false;
        }
        return true;
    }
    const std::int64_t now = sys::monotonic_us();
    if (starting_since_us_ == 0)
        starting_since_us_ = now;
    if (net == Net::refused && !payload_sent_)
    {
        payload_sent_ = true;
        if (!send_payload())
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
    std::int64_t last_config = 0, last_selftest = 0, last_payloads = 0, last_profile_status = 0,
                 last_store = 0, last_log = 0, last_klog = 0;
    std::int64_t last_backdrops = 0, last_packages = 0, last_pkg_install = 0, last_pkg_queue = 0;
    bool packages_open = false;
    std::int64_t last_ps4mount = 0;
    bool ps4_open = false;
    std::int64_t last_storage = 0;
    bool storage_open = false;
    std::int64_t last_playtime = 0, last_save_job = 0;
    bool playtime_open = false, saves_open = false, backups_open = false, backup_deleting = false;
    std::int64_t last_fs_job = 0;
    bool files_open_ = false;
    std::int64_t last_extras = 0, last_record = 0, last_klog_files = 0, last_profile = 0;
    std::string extras_polled_;
    std::int64_t last_disc = 0, last_dump = 0, last_dumps = 0;
    bool disc_open = false;
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
                last_selftest = last_payloads = last_store = last_log = last_klog = last_backdrops =
                    0;
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
        // One cover per pass (the loop sleeps 100 ms): about 380 KB of PNG each, decoded and shrunk
        // here so the frame only uploads 100 KiB.
        if (now - last_cover >= 250000)
        {
            last_cover = now;
            // the Profil page's pictures are small (JPEGs of about 40 KB): several per pass
            const int burst = watching_.load() == Watch::profile ? 4 : 1;
            for (int i = 0; i < burst; ++i)
                fetch_cover();
        }
        // The pictures behind the areas: a small list every 5 s, so one uploaded in the browser
        // shows up soon; a changed picture is fetched in the same pass (one at a time, about 150 KB
        // of JPEG).
        if (now - last_backdrops >= 5000000)
        {
            last_backdrops = now;
            poll_backdrops();
        }
        fetch_backdrop();
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
        // Each page's data only while it is open (the config also once at the start: the settings
        // page opens filled).
        if (config_wanted_.exchange(false) ||
            (page == Watch::settings && now - last_config >= 15000000))
        {
            last_config = now;
            poll_config();
            if (page == Watch::settings)
                poll_rules();
        }
        poll_language();
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
            const bool profile_runs = [&]
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return data_.profile_run.running;
            }();
            if (now - last_payloads >= 4000000)
            {
                last_payloads = now;
                poll_payloads();
                poll_profiles(false);
            }
            else if (profile_runs && now - last_profile_status >= 1200000)
            {
                last_profile_status = now;
                poll_profiles(true);
            }
            const bool storing = [&]
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return data_.store_job.state == "running" || data_.store_fetching ||
                       !data_.store_loaded;
            }();
            if (now - last_store >= (storing ? 1500000 : 10000000))
            {
                last_store = now;
                poll_store();
            }
        }
        // Pakete: the list while a search runs (every second) or now and then, the install and the
        // queue faster while they run. The first visit with nothing searched yet starts a search,
        // as the web page does.
        if (page == Watch::packages)
        {
            const bool entered = !packages_open;
            packages_open = true;
            PackageScan scan;
            bool installing = false, queue_running = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                scan = data_.package_scan;
                installing = data_.package_install.active;
                queue_running = data_.package_queue.running;
            }
            if (entered || now - last_packages >= (scan.scanning ? 1000000 : 20000000))
            {
                last_packages = now;
                poll_packages(false);
                std::lock_guard<std::mutex> lock(mutex_);
                scan = data_.package_scan;
            }
            if (scan.loaded && !scan.ever && !scan.scanning)
                poll_packages(true);
            bool splitting = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                splitting = data_.package_split.active;
            }
            if (entered || now - last_pkg_install >= (installing || splitting ? 1000000 : 4000000))
            {
                last_pkg_install = now;
                poll_package_install();
                poll_package_split();
            }
            if (entered || now - last_pkg_queue >= (queue_running ? 1500000 : 5000000))
            {
                last_pkg_queue = now;
                poll_package_queue();
            }
        }
        else
        {
            packages_open = false;
        }
        // Disc: the drive every 2 s while it is switched on (10 s off), the dump every second while
        // one runs (4 s otherwise), the list of dumps while a search runs. Away from the page only
        // the drive, every 30 s: whether the console has one at all decides whether the tab is
        // there.
        if (page == Watch::disc)
        {
            const bool entered = !disc_open;
            disc_open = true;
            bool drive_on = false, dumping = false, searching = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                drive_on = data_.disc.enabled;
                dumping = data_.dump.job.active || data_.dump.queue.active;
                searching = data_.dumps.scanning;
            }
            if (entered || now - last_disc >= (drive_on ? 2000000 : 10000000))
            {
                last_disc = now;
                poll_disc();
            }
            if (entered || now - last_dump >= (dumping ? 1000000 : 4000000))
            {
                last_dump = now;
                poll_dump();
            }
            if (entered || (searching && now - last_dumps >= 2000000))
            {
                last_dumps = now;
                poll_dumps(false);
                bool never = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    never = data_.dumps.loaded && !data_.dumps.ever && !data_.dumps.scanning;
                }
                if (entered && never)
                    poll_dumps(true);
            }
        }
        else
        {
            disc_open = false;
            if (last_disc == 0 || now - last_disc >= 30000000)
            {
                last_disc = now;
                poll_disc();
            }
        }
        // Spielzeit: every 5 s while it is open, as the web page; Spielstände: the list on entry
        // and after a job, the job every second while one runs; Sicherungen: on entry, and again
        // when a deletion has ended.
        if (page == Watch::playtime)
        {
            if (!playtime_open || now - last_playtime >= 5000000)
            {
                last_playtime = now;
                poll_playtime();
            }
            playtime_open = true;
        }
        else
        {
            playtime_open = false;
        }
        if (page == Watch::saves)
        {
            bool job = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                job = data_.saves.job.active;
            }
            if (!saves_open)
                poll_saves();
            else if (now - last_save_job >= (job ? 1000000 : 5000000))
            {
                last_save_job = now;
                poll_save_job();
                bool ended = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ended = job && !data_.saves.job.active;
                }
                if (ended)
                    poll_saves();
            }
            saves_open = true;
        }
        else
        {
            saves_open = false;
        }
        if (page == Watch::backups)
        {
            bool deleting = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                deleting = data_.jobs[static_cast<std::size_t>(JobKind::deletion)].active;
            }
            if (!backups_open || (backup_deleting && !deleting))
                poll_game_backups();
            backup_deleting = deleting;
            backups_open = true;
        }
        else
        {
            backups_open = false;
        }
        // Extras of a game: every second while a job or a recording step runs there, every 5 s
        // otherwise.
        {
            std::string id;
            bool busy = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                id = extras_id_;
                const ExtrasState &e = data_.extras;
                busy = e.ampr_job.find("\"active\":true") != std::string::npos ||
                       e.image_job.find("\"active\":true") != std::string::npos ||
                       e.trace.find("\"busy\":true") != std::string::npos;
            }
            if (!id.empty() &&
                (id != extras_polled_ || now - last_extras >= (busy ? 1000000 : 5000000)))
            {
                last_extras = now;
                extras_polled_ = id;
                poll_extras(id);
            }
            if (id.empty())
                extras_polled_.clear();
        }
        // Protokoll: whether the kernel log is being recorded, every 2 s.
        if (page == Watch::profile && now - last_profile >= 10000000)
        {
            last_profile = now;
            poll_profile();
            poll_own_pictures(); // a stick put in shows its pictures within a few seconds
        }
        if (page == Watch::logs && now - last_record >= 2000000)
        {
            last_record = now;
            poll_klog_record();
            if (now - last_klog_files >= 6000000)
            {
                last_klog_files = now;
                poll_klog_files();
            }
        }
        // Dateien: the job every 0.8 s while one runs (3 s otherwise); the open folder again when
        // it has just ended.
        if (page == Watch::files)
        {
            bool active = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                active = data_.fs_job.active;
            }
            if (!files_open_ || now - last_fs_job >= (active ? 800000 : 3000000))
            {
                last_fs_job = now;
                if (!files_open_)
                    poll_fs_places();
                poll_fs_job();
                bool ended = false;
                std::string folder;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ended = active && !data_.fs_job.active;
                    folder = data_.fs_list.path;
                }
                if (ended)
                    poll_fs_list(folder);
            }
            files_open_ = true;
        }
        else
        {
            files_open_ = false;
        }
        // System → Speicher: the drives every 4 s, every second while an eject runs; at once when
        // it opens.
        if (page == Watch::storage)
        {
            bool ejecting = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ejecting = data_.usb_eject.running;
            }
            if (!storage_open || now - last_storage >= (ejecting ? 1000000 : 4000000))
            {
                last_storage = now;
                poll_storage();
                poll_system_extra(!storage_open);
            }
            storage_open = true;
        }
        else
        {
            storage_open = false;
        }
        // PS4 von USB: every 5 s while it is open, as the web page; at once when it opens.
        if (page == Watch::ps4mount)
        {
            if (!ps4_open || now - last_ps4mount >= 5000000)
            {
                last_ps4mount = now;
                poll_ps4mount();
            }
            ps4_open = true;
        }
        else
        {
            ps4_open = false;
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
        if (exchange("POST", "/api/v1/fan/threshold", payload, &status, &body) == Net::ok &&
            status == 200)
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
        const Net net = exchange("POST", "/api/v1/library/launch", text != nullptr ? text : "{}",
                                 &status, &body, 8);
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
            notice(Notice::Kind::error, "Starten nicht möglich",
                   message_of(body, "Unbekannter Fehler."));
        }
        cJSON_Delete(reply);
        break;
    }
    case Request::Kind::close:
    {
        std::snprintf(payload, sizeof(payload), "{\"id\":\"%.20s\"}", request.id.c_str());
        // On success the screen has said it already ("… wird beendet, danach startet …"); only a
        // refusal is news.
        if (exchange("POST", "/api/v1/library/close", payload, &status, &body) != Net::ok ||
            status >= 300)
            notice(Notice::Kind::error, "Beenden nicht möglich",
                   message_of(body, "Unbekannter Fehler."));
        break;
    }
    case Request::Kind::remount:
    {
        std::snprintf(payload, sizeof(payload), "{\"id\":\"%.20s\",\"action\":\"%.10s\"}",
                      request.id.c_str(), request.text.c_str());
        // ShadowMountPlus takes some seconds for an image; the answer says what happened
        if (exchange("POST", "/api/v1/library/remount", payload, &status, &body, 70) == Net::ok &&
            status < 300)
            notice(Notice::Kind::success, request.id, message_of(body, "Erledigt."));
        else
            notice(Notice::Kind::error, "Aus- oder Einhängen nicht möglich",
                   message_of(body, "Unbekannter Fehler."));
        poll_library();
        break;
    }
    case Request::Kind::cancel:
    {
        // A delete has no way back once it started: the screen never asks to cancel one.
        static const char *const kRoutes[] = {
            "/api/v1/library/copy/cancel", "/api/v1/library/convert/cancel",
            "/api/v1/library/image/cancel", "/api/v1/library/storage/cancel"};
        const int index = std::clamp(request.value, 0, 3);
        if (exchange("POST", kRoutes[index], "{}", &status, &body) == Net::ok && status < 300)
            notice(Notice::Kind::info, "Auftrag wird abgebrochen");
        else
            notice(Notice::Kind::error, "Abbrechen nicht möglich",
                   message_of(body, "Unbekannter Fehler."));
        poll_jobs();
        break;
    }
    case Request::Kind::web:
        if (exchange("POST", "/api/v1/open-ui", "{\"from\":\"fullscreen\"}", &status, &body) == Net::ok && status < 300)
            notice(Notice::Kind::info, "Weboberfläche", "Wird im Browser der Konsole geöffnet.");
        else
            notice(Notice::Kind::error, "Browser nicht geöffnet",
                   message_of(body, "Unbekannter Fehler."));
        break;
    case Request::Kind::reload:
        notice(Notice::Kind::info, "Daten werden neu geladen");
        break;
    case Request::Kind::config:
        if (exchange("PUT", "/api/v1/config", request.text, &status, &body) == Net::ok &&
            status < 300)
            notice(Notice::Kind::success, "Gespeichert");
        else
            notice(Notice::Kind::error, "Nicht gespeichert",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_config();
        poll_status();
        break;
    case Request::Kind::rule:
        if (exchange("PUT", "/api/v1/games", request.text, &status, &body) == Net::ok &&
            status < 300)
            notice(Notice::Kind::success,
                   request.value != 0 ? "Spielprofil gespeichert" : "Spielprofil entfernt",
                   request.value != 0 ? request.id : std::string());
        else
            notice(Notice::Kind::error, "Spielprofil nicht geändert",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_rules();
        break;
    case Request::Kind::selftest:
        if (exchange("POST", "/api/v1/fan/selftest",
                     request.value != 0 ? "{\"action\":\"start\"}" : "{\"action\":\"abort\"}",
                     &status, &body) != Net::ok ||
            status >= 300)
            notice(Notice::Kind::error,
                   request.value != 0 ? "Selbsttest nicht möglich" : "Abbrechen nicht möglich",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_selftest();
        break;
    case Request::Kind::power:
    {
        std::snprintf(payload, sizeof(payload), "{\"action\":\"%.16s\"}", request.id.c_str());
        if (exchange("POST", "/api/v1/power", payload, &status, &body) == Net::ok && status < 300)
            notice(Notice::Kind::info, "Befehl angenommen", message_of(body, ""));
        else
            notice(Notice::Kind::error, "Nicht ausgeführt",
                   message_of(body, "Die App hat nicht geantwortet."));
        break;
    }
    case Request::Kind::payload_start:
        if (exchange("POST", "/api/v1/payload-files/start", request.text, &status, &body, 10) ==
                Net::ok &&
            status < 300)
            notice(Notice::Kind::success, "„" + request.id + "“ gestartet",
                   "An den ELF-Loader gesendet.");
        else
            notice(Notice::Kind::error, "„" + request.id + "“ nicht gestartet",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_payloads();
        break;
    case Request::Kind::payload_post:
    {
        static const char *const kRoute[] = {
            "/api/v1/payload-files/copy", "/api/v1/payload-files/delete",
            "/api/v1/payload-profiles", "/api/v1/payload-profiles/run",
            "/api/v1/payload-profiles/stop"};
        static const char *const kDone[] = {"kopiert", "gelöscht", "Profile gespeichert",
                                            "Profil gestartet", "Profil wird angehalten"};
        static const char *const kFail[] = {"Kopieren nicht möglich", "Löschen nicht möglich",
                                            "Profile nicht gespeichert", "Profil nicht gestartet",
                                            "Anhalten nicht möglich"};
        const int v = std::clamp(request.value, 0, 4);
        // copying waits for the drive: the app allows itself up to two minutes for it
        if (exchange("POST", kRoute[v], request.text, &status, &body, v == 0 ? 130 : 10) ==
                Net::ok &&
            status < 300)
        {
            if (v <= 1)
                notice(Notice::Kind::success, "„" + request.id + "“ " + kDone[v]);
            else
                notice(v == 4 ? Notice::Kind::info : Notice::Kind::success, kDone[v]);
        }
        else
            notice(Notice::Kind::error, kFail[v],
                   message_of(body, "Die App hat nicht geantwortet."));
        if (v <= 1)
            poll_payloads();
        poll_profiles(false);
        break;
    }
    case Request::Kind::payload_kill:
        std::snprintf(payload, sizeof(payload), "{\"pid\":%d}", request.value);
        if (exchange("POST", "/api/v1/payloads/kill", payload, &status, &body) == Net::ok &&
            status < 300)
            notice(Notice::Kind::info, "Payload beendet");
        else
            notice(Notice::Kind::error, "Beenden nicht möglich",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_payloads();
        break;
    case Request::Kind::package:
    {
        const std::string &a = request.text;
        std::string route, json = "{}";
        if (a == "scan")
        {
            poll_packages(true);
            break;
        }
        if (a == "split" || a == "split_cancel")
        {
            const bool split = a == "split";
            if (exchange("POST", split ? "/api/v1/packages/split" : "/api/v1/packages/cancel",
                         split ? request.id : std::string("{}"), &status, &body, 10) == Net::ok &&
                status < 300)
                notice(Notice::Kind::info,
                       split ? "Paket wird aufgeteilt" : "Aufteilen wird abgebrochen");
            else
                notice(Notice::Kind::error,
                       split ? "Aufteilen nicht gestartet" : "Abbrechen nicht möglich",
                       message_of(body, "Die App hat nicht geantwortet."));
            poll_package_split();
            break;
        }
        if (a == "install")
            route = "/api/v1/packages/install";
        else if (a == "install_cancel")
            route = "/api/v1/packages/install/cancel";
        else if (a == "queue_clear")
        {
            route = "/api/v1/packages/queue/clear";
            json = "{\"all\":false}";
        }
        else if (a.rfind("queue_", 0) == 0)
            route = "/api/v1/packages/queue/" + a.substr(6);
        else
            break;
        if (!request.id.empty())
        {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "id", request.id.c_str());
            char *text = cJSON_PrintUnformatted(root);
            json = text != nullptr ? text : "{}";
            cJSON_free(text);
            cJSON_Delete(root);
        }
        const Net net = exchange("POST", route.c_str(), json, &status, &body, 10);
        if (net != Net::ok || status >= 300)
        {
            const char *title = a == "install"          ? "Installation nicht gestartet"
                                : a == "install_cancel" ? "Abbrechen nicht möglich"
                                                        : "Warteschlange";
            notice(Notice::Kind::error, title, message_of(body, "Die App hat nicht geantwortet."));
        }
        else if (a == "queue_add")
        {
            cJSON *reply = cJSON_Parse(body.c_str());
            const int added = reply != nullptr ? static_cast<int>(num(reply, "added")) : 0;
            const std::string msg = reply != nullptr ? str(reply, "message") : "";
            cJSON_Delete(reply);
            if (added > 0)
                notice(Notice::Kind::success, "In die Warteschlange gelegt");
            else
                notice(Notice::Kind::info, "Schon in der Warteschlange", msg);
        }
        else if (a == "install")
        {
            notice(Notice::Kind::success, "Installation gestartet");
        }
        poll_package_install();
        poll_package_queue();
        break;
    }
    case Request::Kind::ps4mount:
    {
        const std::string route =
            request.id.empty() ? "/api/v1/ps4mount" : "/api/v1/ps4mount/" + request.id;
        // Adding, removing and moving back can take a while: PS4Mount answers when it is done.
        const Net net = exchange("POST", route.c_str(), request.text, &status, &body, 30);
        if (net != Net::ok || status >= 300)
            notice(Notice::Kind::error, "PS4Mount",
                   message_of(body, "Die App hat nicht geantwortet."));
        else
        {
            const std::string msg = message_of(body, "");
            if (!msg.empty())
                notice(Notice::Kind::success, "PS4Mount", msg);
        }
        poll_ps4mount();
        break;
    }
    case Request::Kind::ps4mount_fetch:
        if (request.text == "game")
            fetch_ps4_detail(request.id);
        else
            fetch_ps4_log();
        break;
    case Request::Kind::disc:
    {
        const std::string route = "/api/v1/" + request.id;
        const Net net = exchange("POST", route.c_str(), request.text, &status, &body, 15);
        if (net != Net::ok || status >= 300)
            notice(Notice::Kind::error, request.id == "disc" ? "Laufwerk" : "Dump",
                   message_of(body, "Die App hat nicht geantwortet."));
        else
        {
            const std::string msg = message_of(body, "");
            if (!msg.empty())
                notice(Notice::Kind::success, request.id == "disc" ? "Laufwerk" : "Dump", msg);
        }
        if (request.id == "disc")
            poll_disc();
        else if (request.id == "dump")
            poll_dump();
        else
            poll_dumps(false);
        break;
    }
    case Request::Kind::playtime_reset:
    {
        const Net net = exchange("POST", "/api/v1/playtime/reset", "{}", &status, &body, 10);
        if (net != Net::ok || status >= 300)
            notice(Notice::Kind::error, "Spielzeit",
                   message_of(body, "Die App hat nicht geantwortet."));
        else
            notice(Notice::Kind::success, "Spielzeit", "Der Verlauf ist gelöscht.");
        poll_playtime();
        break;
    }
    case Request::Kind::saves:
    {
        const std::string route = "/api/v1/saves/" + request.id;
        const Net net = exchange("POST", route.c_str(), request.text, &status, &body, 20);
        if (net != Net::ok || status >= 300)
            notice(Notice::Kind::error, "Spielstände",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_save_job();
        break;
    }
    case Request::Kind::backup_delete:
    {
        // As the web app's delete dialog: the plan says whether it may go and brings the one-time
        // word; then at once.
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "kind", "backup");
        cJSON_AddStringToObject(root, "id", request.id.c_str());
        char *text = cJSON_PrintUnformatted(root);
        const std::string plan_body = text != nullptr ? text : "{}";
        cJSON_free(text);
        cJSON_Delete(root);
        const Net net =
            exchange("POST", "/api/v1/library/delete/plan", plan_body, &status, &body, 120);
        cJSON *plan = net == Net::ok && status == 200 ? cJSON_Parse(body.c_str()) : nullptr;
        if (plan == nullptr)
        {
            notice(Notice::Kind::error, "Sicherung",
                   message_of(body, "Die App hat keinen Plan geliefert."));
            break;
        }
        const bool can = flag(plan, "can_delete");
        const std::string why = str(plan, "why"), confirm = str(plan, "confirm"),
                          name = str(plan, "name");
        cJSON_Delete(plan);
        if (!can)
        {
            notice(Notice::Kind::error, "Sicherung",
                   why.empty() ? "Diese Sicherung lässt sich nicht löschen." : why);
            break;
        }
        root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "kind", "backup");
        cJSON_AddStringToObject(root, "id", request.id.c_str());
        cJSON_AddStringToObject(root, "confirm", confirm.c_str());
        text = cJSON_PrintUnformatted(root);
        const std::string start_body = text != nullptr ? text : "{}";
        cJSON_free(text);
        cJSON_Delete(root);
        const Net started =
            exchange("POST", "/api/v1/library/delete", start_body, &status, &body, 20);
        if (started != Net::ok || status >= 300)
            notice(Notice::Kind::error, "Sicherung",
                   message_of(body, "Die App hat nicht geantwortet."));
        else
            notice(Notice::Kind::info, "Sicherung", "„" + name + "“ wird gelöscht.");
        break;
    }
    case Request::Kind::extras_post:
    {
        const std::string route = "/api/v1/" + request.id;
        const Net net = exchange("POST", route.c_str(), request.text, &status, &body, 30);
        if (net != Net::ok || status >= 300)
            notice(Notice::Kind::error, "Extras",
                   message_of(body, "Die App hat nicht geantwortet."));
        else
        {
            const std::string msg = message_of(body, "");
            if (!msg.empty())
                notice(Notice::Kind::success, "Extras", msg);
        }
        std::string id;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            id = extras_id_;
        }
        if (!id.empty())
            poll_extras(id);
        break;
    }
    case Request::Kind::extras_versions:
    {
        const char *route = request.id == "playgo" ? "/api/v1/playgo" : "/api/v1/ampr-emu";
        const Net net = exchange("GET", route, {}, &status, &body, 10);
        std::lock_guard<std::mutex> lock(mutex_);
        data_.extras_versions.seq++;
        data_.extras_versions.lib = request.id;
        data_.extras_versions.json = net == Net::ok && status == 200 ? body : std::string();
        ++data_.serial;
        break;
    }
    case Request::Kind::extras_plan:
    {
        const std::string route = "/api/v1/library/lz4/plan?id=" + url_encoded(request.id) +
                                  "&level=" + std::to_string(request.value) +
                                  "&profile=" + url_encoded(request.text) +
                                  "&format=" + url_encoded(request.extra.empty() ? "lz4" : request.extra);
        const Net net = exchange("GET", route.c_str(), {}, &status, &body, 30);
        std::lock_guard<std::mutex> lock(mutex_);
        data_.extras_lz4_plan.seq++;
        data_.extras_lz4_plan.json = net == Net::ok && status == 200 ? body : std::string();
        ++data_.serial;
        break;
    }
    case Request::Kind::lib_tools:
        poll_library_tools();
        break;
    case Request::Kind::sys_post:
    {
        const std::string route = "/api/v1/" + request.id;
        if (exchange("POST", route.c_str(), request.text.empty() ? "{}" : request.text, &status,
                     &body, 20) == Net::ok &&
            status < 300)
        {
            const std::string msg = message_of(body, "");
            notice(Notice::Kind::success, "System", msg.empty() ? "Erledigt." : msg);
        }
        else
            notice(Notice::Kind::error, "System",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_status();
        poll_system_extra(false);
        break;
    }
    case Request::Kind::risky:
    {
        std::string risky;
        const Net net = exchange("GET", "/api/v1/sensors/risky", {}, &status, &body, 10);
        if (net == Net::ok && status == 200)
            risky = body;
        else if (net != Net::ok)
            notice(Notice::Kind::error, "Risikotelemetrie", "Die App hat nicht geantwortet.");
        else
            notice(Notice::Kind::error, "Risikotelemetrie",
                   message_of(body, "Die Abfrage ist aus (Zusatzabfragen → Risiko)."));
        std::lock_guard<std::mutex> lock(mutex_);
        data_.system_data.risky = std::move(risky);
        ++data_.system_data.seq;
        ++data_.serial;
        break;
    }
    case Request::Kind::lib_tools_post:
    {
        const std::string route = "/api/v1/" + request.id;
        const Net net = exchange("POST", route.c_str(), request.text, &status, &body, 30);
        if (net != Net::ok || status >= 300)
            notice(Notice::Kind::error, "Bibliothek",
                   message_of(body, "Die App hat nicht geantwortet."));
        else
        {
            const std::string msg = message_of(body, "");
            notice(Notice::Kind::success, "Bibliothek", msg.empty() ? "Erledigt." : msg);
        }
        poll_library_tools();
        break;
    }
    case Request::Kind::klog_save:
    {
        char path[64];
        std::snprintf(path, sizeof(path), "/api/v1/klog/save?tz=%d", request.value);
        if (exchange("POST", path, request.text, &status, &body, 30) == Net::ok && status < 300)
        {
            std::string name;
            if (cJSON *root = cJSON_Parse(body.c_str()))
            {
                name = str(root, "name");
                cJSON_Delete(root);
            }
            notice(Notice::Kind::success, "Auszug gespeichert",
                   name.empty() ? std::string("Ordner klog-live-log") : name);
        }
        else
            notice(Notice::Kind::error, "Auszug nicht gespeichert",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_klog_files();
        break;
    }
    case Request::Kind::klog_clear:
        if (exchange("POST", "/api/v1/klog/files/clear", request.text, &status, &body, 10) ==
                Net::ok &&
            status < 300)
            notice(Notice::Kind::success, "Ordner geleert",
                   "Die laufende Aufnahme bleibt, wenn eine läuft.");
        else
            notice(Notice::Kind::error, "Ordner nicht geleert",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_klog_files();
        break;
    case Request::Kind::profile_name:
        if (exchange("POST", "/api/v1/profile/username", request.text, &status, &body, 10) ==
                Net::ok &&
            status < 300)
            notice(Notice::Kind::success, "Name geändert",
                   "Im Startmenü erscheint er nach einem Benutzerwechsel.");
        else
            notice(Notice::Kind::error, "Name nicht geändert",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_profile();
        break;
    case Request::Kind::avatar_apply:
    {
        // As buildAvatarFiles in the web page: the picture from the console app, made into the
        // DDS and PNG files here, each sent, then applied.
        std::string name;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            name = data_.profile.username;
        }
        std::string fail;
        // value 1: a picture of one's own, read through the file manager (up to 16 MB)
        const std::string path = request.value == 1
                                     ? "/api/v1/files/view?path=" + url_encoded(request.id)
                                     : "/avatars/" + path_encoded(request.id);
        if (exchange("GET", path.c_str(), {}, &status, &body, 30) != Net::ok || status != 200)
            fail = "Das Bild ließ sich nicht laden.";
        std::vector<std::pair<std::string, std::vector<std::uint8_t>>> files;
        if (fail.empty())
        {
            files = avatar_files(reinterpret_cast<const std::uint8_t *>(body.data()), body.size(),
                                 name);
            if (files.empty())
                fail = "Das Bild ließ sich nicht umrechnen.";
        }
        for (const auto &[file, bytes] : files)
        {
            if (!fail.empty())
                break;
            // the name as it is: the console app compares it with its list (avatar_name_ok)
            const std::string route = "/api/v1/profile/avatar/file?name=" + path_encoded(file);
            if (exchange("POST", route.c_str(), std::string(bytes.begin(), bytes.end()), &status,
                         &body, 15) != Net::ok ||
                status >= 300)
                fail = message_of(body, "Eine Datei des Bildes kam nicht an.");
        }
        if (fail.empty() && (exchange("POST", "/api/v1/profile/avatar/apply", "{}", &status, &body,
                                      15) != Net::ok ||
                             status >= 300))
            fail = message_of(body, "Die App hat das Bild nicht übernommen.");
        if (fail.empty())
            notice(Notice::Kind::success, "Profilbild übernommen",
                   "Im Startmenü erscheint es nach einem Benutzerwechsel.");
        else
            notice(Notice::Kind::error, "Profilbild nicht übernommen", fail);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            data_.avatar_busy = false;
            if (fail.empty())
                ++data_.avatar_revision;
            ++data_.serial;
        }
        break;
    }
    case Request::Kind::avatar_restore:
        if (exchange("POST", "/api/v1/profile/avatar/restore", request.text, &status, &body, 15) ==
                Net::ok &&
            status < 300)
        {
            notice(Notice::Kind::success, "Vorheriges Profilbild zurückgeholt");
            std::lock_guard<std::mutex> lock(mutex_);
            ++data_.avatar_revision;
            ++data_.serial;
        }
        else
            notice(Notice::Kind::error, "Nicht zurückgeholt",
                   message_of(body, "Die App hat nicht geantwortet."));
        break;
    case Request::Kind::account_post:
    {
        const std::string &action = request.id;
        const bool ok = exchange("POST", ("/api/v1/profile/account/" + action).c_str(),
                                 request.text, &status, &body, 20) == Net::ok &&
                        status < 300;
        const bool activate = action == "activate";
        const bool fake = action == "fakesignin";
        if (ok)
            notice(Notice::Kind::success,
                   activate ? "Offline-Konto aktiviert"
                   : fake   ? "Fake-Anmeldung gesendet"
                            : "Konto zurückgesetzt",
                   activate ? "Die Konsole zeigt jetzt diesen Namen."
                   : fake   ? "Die Meldung der Konsole erscheint gleich."
                            : "");
        else
            notice(Notice::Kind::error,
                   activate ? "Offline-Konto nicht aktiviert"
                   : fake   ? "Fake-Anmeldung nicht gesendet"
                            : "Nicht zurückgesetzt",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_profile();
        break;
    }
    case Request::Kind::klog_record:
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "on", request.value != 0);
        int tz = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tz = data_.config.tz_offset_min;
        }
        cJSON_AddNumberToObject(root, "tz", tz);
        char *text = cJSON_PrintUnformatted(root);
        const std::string payload = text != nullptr ? text : "{}";
        cJSON_free(text);
        cJSON_Delete(root);
        const Net net = exchange("POST", "/api/v1/klog/record", payload, &status, &body, 10);
        if (net != Net::ok || status >= 300)
            notice(Notice::Kind::error, "Kernel-Log",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_klog_record();
        break;
    }
    case Request::Kind::fs_open:
        if (request.id.empty())
            poll_fs_places();
        poll_fs_list(request.id);
        break;
    case Request::Kind::fs_post:
    {
        const std::string route = "/api/v1/files/" + request.id;
        // copy and move count everything first: up to two minutes, as the web app waits
        const Net net = exchange("POST", route.c_str(), request.text, &status, &body, 120);
        if (net != Net::ok || status >= 300)
        {
            std::string msg = message_of(body, "Die App hat nicht geantwortet.");
            // the names that are in the way: the screen asks what to do with them
            std::vector<std::string> taken;
            if (status == 409 && (request.id == "copy" || request.id == "move"))
            {
                if (cJSON *root = cJSON_Parse(body.c_str()))
                {
                    if (str(root, "code") == "exists")
                    {
                        const cJSON *c = nullptr;
                        cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(root, "conflicts"))
                        {
                            if (cJSON_IsString(c) && c->valuestring != nullptr)
                                taken.emplace_back(c->valuestring);
                        }
                    }
                    cJSON_Delete(root);
                }
            }
            if (!taken.empty())
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_conflict_route_ = request.id;
                pending_conflict_json_ = request.text;
                data_.fs_conflict.names = std::move(taken);
                data_.fs_conflict.move = request.id == "move";
                ++data_.fs_conflict.seq;
                ++data_.serial;
                break;
            }
            if (cJSON *root = cJSON_Parse(body.c_str()))
            {
                const cJSON *c = nullptr;
                int n = 0;
                cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(root, "conflicts"))
                {
                    if (cJSON_IsString(c) && n++ < 5)
                        msg += std::string(n == 1 ? " (" : ", ") + c->valuestring;
                }
                if (n > 0)
                    msg += n > 5 ? " …)" : ")";
                cJSON_Delete(root);
            }
            notice(Notice::Kind::error, "Dateien", msg);
        }
        poll_fs_job();
        std::string folder;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            folder = data_.fs_list.path;
        }
        if (request.id == "mkdir" || request.id == "rename")
            poll_fs_list(folder);
        break;
    }
    case Request::Kind::fs_delete:
    {
        // As the web page: the plan brings the one-time word for exactly these paths; then the
        // delete, at once.
        const Net net =
            exchange("POST", "/api/v1/files/delete/plan", request.text, &status, &body, 60);
        cJSON *plan = net == Net::ok && status == 200 ? cJSON_Parse(body.c_str()) : nullptr;
        if (plan == nullptr)
        {
            notice(Notice::Kind::error, "Dateien",
                   message_of(body, "Die App hat keinen Plan geliefert."));
            break;
        }
        const std::string confirm = str(plan, "confirm");
        cJSON *root = cJSON_Parse(request.text.c_str());
        if (root != nullptr)
            cJSON_AddStringToObject(root, "confirm", confirm.c_str());
        char *text = root != nullptr ? cJSON_PrintUnformatted(root) : nullptr;
        const std::string start = text != nullptr ? text : "{}";
        cJSON_free(text);
        cJSON_Delete(root);
        cJSON_Delete(plan);
        const Net started = exchange("POST", "/api/v1/files/delete", start, &status, &body, 20);
        if (started != Net::ok || status >= 300)
            notice(Notice::Kind::error, "Dateien",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_fs_job();
        break;
    }
    case Request::Kind::fs_view:
    {
        FsView v;
        v.path = request.id;
        const std::string path = "/api/v1/files/view?path=" + url_encoded(request.id);
        const Net net = exchange("GET", path.c_str(), {}, &status, &body, 20);
        if (net == Net::ok && status == 200)
        {
            v.text = std::move(body);
            v.size = v.text.size();
            // the console sends only the beginning (256 KB); X-Fm-Truncated is not read here, the
            // size tells it
            v.truncated = v.size >= 256 * 1024;
        }
        else
            v.error = net == Net::ok ? message_of(body, "Die Datei lässt sich nicht ansehen.")
                                     : "Die App hat nicht rechtzeitig geantwortet.";
        std::lock_guard<std::mutex> lock(mutex_);
        v.seq = data_.fs_view.seq + 1;
        data_.fs_view = std::move(v);
        ++data_.serial;
        break;
    }
    case Request::Kind::fs_size:
    {
        const std::string path = "/api/v1/files/size?path=" + url_encoded(request.id);
        const Net net = exchange("GET", path.c_str(), {}, &status, &body, 20);
        FsSize s;
        cJSON *root = net == Net::ok && status == 200 ? cJSON_Parse(body.c_str()) : nullptr;
        if (root == nullptr)
            s.failed = true;
        else
        {
            s.bytes = static_cast<std::uint64_t>(num(root, "bytes"));
            s.files = static_cast<int>(num(root, "files"));
            s.partial = flag(root, "partial");
            cJSON_Delete(root);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        data_.fs_sizes[request.id] = s;
        ++data_.serial;
        break;
    }
    case Request::Kind::eject:
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "mount", request.id.c_str());
        char *text = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        const Net net = exchange("POST", "/api/v1/storage/eject", text != nullptr ? text : "{}",
                                 &status, &body, 15);
        cJSON_free(text);
        // A refusal (a job of the app runs, PS4Mount games on the drive) says why; the end comes
        // with the next poll.
        if (net != Net::ok || status >= 300)
            notice(Notice::Kind::error, "Nicht ausgeworfen",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_storage();
        break;
    }
    case Request::Kind::backdrop:
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "area", request.id.c_str());
        cJSON_AddStringToObject(root, "mode", request.text.c_str());
        char *text = cJSON_PrintUnformatted(root);
        const Net net =
            exchange("POST", "/api/v1/backdrops", text != nullptr ? text : "{}", &status, &body);
        cJSON_free(text);
        cJSON_Delete(root);
        if (net != Net::ok || status >= 300)
            notice(Notice::Kind::error, "Hintergrund nicht geändert",
                   message_of(body, "Die App hat nicht geantwortet."));
        poll_backdrops();
        break;
    }
    case Request::Kind::store:
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "action", request.text.c_str());
        if (!request.id.empty())
            cJSON_AddStringToObject(root, "id", request.id.c_str());
        char *text = cJSON_PrintUnformatted(root);
        const Net net =
            exchange("POST", "/api/v1/paystore", text != nullptr ? text : "{}", &status, &body, 10);
        cJSON_free(text);
        cJSON_Delete(root);
        if (net != Net::ok || status >= 300)
            notice(Notice::Kind::error, "Payload-Store",
                   message_of(body, "Die App hat nicht geantwortet."));
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
    // Clock times: the console's own zone when it named one (daylight saving included); else what a
    // browser last told the app, which stays UTC until someone saves the night mode there.
    c.tz_offset_min =
        console_tz_known_ ? console_tz_min_ : static_cast<int>(num(root, "night_tz_offset_min", 0));
    c.target_temp_c = static_cast<int>(num(root, "target_temp_c", 68));
    c.warning_cpu_c = static_cast<int>(num(root, "warning_cpu_c", 85));
    c.warning_countdown_s = static_cast<int>(num(root, "warning_countdown_s", 120));
    c.lightbar_enabled = num(root, "lightbar_enabled", 0) != 0.0;
    c.lightbar_warn_c = static_cast<int>(num(root, "lightbar_warn_c", 70));
    c.lightbar_hot_c = static_cast<int>(num(root, "lightbar_hot_c", 80));
    c.tv_language = str(root, "tv_language");
    c.smp_version = str(root, "smp_version");
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.config = c;
    ++data_.serial;
}

// The screen's language: the setting, else the console's; on a change the browser's dictionary from
// the app
// (/lang/<xx>.json, about 0.5 MB) and the fullscreen app's own from its folder, both parsed here,
// off the frame.
void HttpSource::poll_language()
{
    if (!console_language_read_ && sys::monotonic_us() >= system_retry_us_)
    {
        int status = 0;
        std::string body;
        if (exchange("GET", "/api/v1/system", {}, &status, &body, 8) != Net::ok || status != 200)
        {
            system_retry_us_ = sys::monotonic_us() + 10000000; // not on every pass
        }
        else
        {
            console_language_read_ = true;
            std::string app_version;
            if (cJSON *root = cJSON_Parse(body.c_str()))
            {
                app_version = str(root, "app_version");
                const cJSON *loc = cJSON_GetObjectItemCaseSensitive(root, "locale");
                console_language_ = static_cast<int>(num(loc, "language", -1));
                if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(loc, "local_offset_min")))
                {
                    console_tz_known_ = true;
                    console_tz_min_ = static_cast<int>(num(loc, "local_offset_min", 0));
                }
                cJSON_Delete(root);
            }
            std::lock_guard<std::mutex> lock(mutex_);
            data_.console_language = console_language_;
            data_.app_version = app_version;
            if (console_tz_known_)
                data_.config.tz_offset_min = console_tz_min_;
            ++data_.serial;
        }
    }
    std::string setting;
    bool config_valid = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        setting = data_.config.tv_language;
        config_valid = data_.config.valid;
    }
    // A language chosen in the menu needs nothing from the console.
    if (!config_valid || (setting.empty() && !console_language_read_))
        return;
    const std::string wanted = i18n::wanted_language(setting, console_language_);
    if (wanted == language_loaded_ || sys::monotonic_us() < language_retry_us_)
        return;
    auto entries = std::make_shared<std::vector<i18n::Entry>>();
    if (wanted != "de")
    {
        int status = 0;
        std::string body;
        const std::string path = "/lang/" + wanted + ".json";
        if (exchange("GET", path.c_str(), {}, &status, &body, 20) != Net::ok || status != 200 ||
            !i18n::parse_dictionary(body, entries.get()))
        {
            sys::log("[PS5CC] dictionary %s not loaded (HTTP %d)", wanted.c_str(), status);
            language_retry_us_ = sys::monotonic_us() + 10000000; // again in 10 s
            return;
        }
        const std::size_t web = entries->size();
        std::string own;
        if (save::read_file("/app0/assets/lang/" + wanted + ".json", &own, 4u * 1024u * 1024u))
            i18n::parse_dictionary(own, entries.get());
        sys::log("[PS5CC] language %s: %zu entries from the app, %zu of its own", wanted.c_str(),
                 web, entries->size() - web);
    }
    language_loaded_ = wanted;
    std::lock_guard<std::mutex> lock(mutex_);
    data_.language = wanted;
    data_.dictionary = std::move(entries);
    ++data_.serial;
}

// GET /api/v1/games: the kept game profiles and which one is in force.
void HttpSource::poll_rules()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/games", {}, &status, &body) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    std::vector<GameRule> rules;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "rules"))
    {
        GameRule r;
        r.title_id = str(item, "title_id");
        r.title_name = str(item, "title_name");
        r.target_c = static_cast<int>(num(item, "target_temp_c", 68));
        r.profile = str(item, "profile");
        if (!r.title_id.empty())
            rules.push_back(std::move(r));
    }
    const std::string active = str(root, "active");
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.game_rules = std::move(rules);
    data_.active_rule = active;
    data_.rules_loaded = true;
    ++data_.serial;
}

// GET /api/v1/payload-profiles (unless only the run is asked for) and .../status.
void HttpSource::poll_profiles(bool status_only)
{
    int status = 0;
    std::string body;
    std::vector<PayloadProfile> profiles;
    std::string startup;
    bool have_profiles = false;
    if (!status_only &&
        exchange("GET", "/api/v1/payload-profiles", {}, &status, &body) == Net::ok && status == 200)
    {
        if (cJSON *root = cJSON_Parse(body.c_str()))
        {
            startup = str(root, "startup");
            const cJSON *p = nullptr;
            cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(root, "profiles"))
            {
                PayloadProfile prof;
                prof.id = str(p, "id");
                prof.name = str(p, "name");
                const cJSON *item = nullptr;
                cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(p, "items"))
                {
                    if (cJSON_IsString(item) && item->valuestring != nullptr)
                        prof.items.emplace_back(item->valuestring);
                }
                if (!prof.id.empty())
                    profiles.push_back(std::move(prof));
            }
            cJSON_Delete(root);
            have_profiles = true;
        }
    }
    ProfileRun run;
    bool have_run = false;
    if (exchange("GET", "/api/v1/payload-profiles/status", {}, &status, &body) == Net::ok &&
        status == 200)
    {
        if (cJSON *root = cJSON_Parse(body.c_str()))
        {
            const cJSON *r = cJSON_GetObjectItemCaseSensitive(root, "running");
            run.running = cJSON_IsTrue(r);
            run.id = str(root, "id");
            run.name = str(root, "name");
            run.step = static_cast<int>(num(root, "step"));
            run.total = static_cast<int>(num(root, "total"));
            const cJSON *e = nullptr;
            cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "results"))
            {
                ProfileRunResult res;
                res.item = str(e, "item");
                res.ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(e, "ok"));
                res.message = str(e, "msg");
                run.results.push_back(std::move(res));
            }
            cJSON_Delete(root);
            have_run = true;
        }
    }
    if (!have_profiles && !have_run)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (have_profiles)
    {
        data_.payload_profiles = std::move(profiles);
        data_.profile_startup = startup;
        data_.profiles_loaded = true;
    }
    if (have_run)
        data_.profile_run = std::move(run);
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
                p.file = str(item, "file");
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
    if (exchange("GET", "/api/v1/paystore?refresh=1", {}, &status, &body, 15) != Net::ok ||
        status != 200)
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
        s.installed_file = str(cJSON_GetObjectItemCaseSensitive(item, "installed"), "filename");
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
        notices_.push_back({Notice::Kind::success,
                            "„" + job.name + "“ geladen",
                            {},
                            {},
                            {},
                            {},
                            {},
                            JobKind::count});
    if (data_.store_job.state == "running" && job.state == "failed")
        notices_.push_back({Notice::Kind::error,
                            "„" + job.name + "“ nicht geladen",
                            job.error,
                            {},
                            {},
                            {},
                            {},
                            JobKind::count});
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
    if (exchange("GET", "/api/v1/logs/tail?count=150", {}, &status, &body) != Net::ok ||
        status != 200)
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
    std::snprintf(path, sizeof(path), "/api/v1/klog?after=%lld&max=%d",
                  static_cast<long long>(after), after == 0 ? 300 : 500);
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
    status_raw_ = body;
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
    s.target_c =
        static_cast<int>(num(control, "effective_target_c", num(control, "target_temp_c", 0)));
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
        // The app's two tiles (and their ids before 1.74.1) are titles of their own, not games to
        // start from here.
        if (g.id == own_title_id() || is_own_tile(g.id))
            continue;
        g.name = str(item, "name");
        g.platform = str(item, "platform");
        g.version = str(item, "version");
        g.format = str(item, "format");
        g.system_ver = str(item, "system_ver");
        {
            // With a backport applied the firmware that matters is the one it carries (11.2026)
            const cJSON *mods = cJSON_GetObjectItemCaseSensitive(item, "mods");
            if (mods != nullptr && str(mods, "state") == "checked" && flag(mods, "backport") && g.platform != "PS4")
                g.backport_fw = str(mods, "eboot_sdk");
        }
        // A PS4Mount game is an installed package to the system; shown as what it is, not as
        // installed (29.10.2026).
        if (g.format == "pkg" && flag(item, "ps4mount"))
            g.format = "ps4mount";
        g.path = str(item, "path");
        g.cover = str(item, "cover");
        // Only the app's own route: the bytes are fetched from 127.0.0.1 and must not lead anywhere
        // else.
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
        g.can_remount = flag(item, "can_remount");
        g.smp_mounted = flag(item, "mounted");
        g.ampr_emu = flag(cJSON_GetObjectItemCaseSensitive(item, "mods"), "ampr_emu");
        g.disc = str(item, "disc");
        g.dump_mounted = flag(item, "dump_mounted");
        g.dump = str(item, "dump");
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
    // Game covers first, then the developer pictures the console app has cached, under keys of
    // their own.
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
        // The PS4Mount games' pictures, only while that page is open.
        if (watching_.load() == Watch::ps4mount)
        {
            for (const Ps4Game &p : data_.ps4mount.games)
            {
                Game g;
                g.id = ps4_key(p.title_id);
                g.cover = "/api/v1/ps4mount/icon?id=" + url_encoded(p.title_id);
                games.push_back(std::move(g));
            }
        }
        // The Profil page: the picture in use, the built-in ones and the people thanked.
        if (watching_.load() == Watch::profile)
        {
            // before the game covers: on this page its own pictures come first
            std::vector<Game> own;
            Game current;
            current.id = profile_picture_key(data_.avatar_revision);
            current.cover = "/api/v1/profile/avatar/current";
            own.push_back(std::move(current));
            for (const OwnPicture &o : data_.own_pictures)
            {
                Game g;
                g.id = own_picture_key(o.path);
                g.cover = "/api/v1/files/view?path=" + url_encoded(o.path);
                own.push_back(std::move(g));
            }
            for (const AvatarChoice &a : data_.avatars)
            {
                Game g;
                g.id = avatar_choice_key(a.file);
                // the full picture (440 px, about 40 KB): the 132 px preview looked blurred at
                // the size the grid draws it (user, 31.10.2026)
                g.cover = "/avatars/" + path_encoded(a.file);
                own.push_back(std::move(g));
            }
            for (const Credit &c : kCredits)
            {
                if (c.picture[0] == 0)
                    continue;
                Game g;
                g.id = credit_key(c.picture);
                g.cover = "/credits/" + path_encoded(c.picture);
                own.push_back(std::move(g));
            }
            games.insert(games.begin(), own.begin(), own.end());
        }
        // The pictures of the packages, only while the Pakete page is open (ids hold for one
        // search).
        if (watching_.load() == Watch::packages)
        {
            for (const Package &p : data_.packages)
            {
                if (!p.has_icon)
                    continue;
                Game g;
                g.id = package_key(p.id);
                g.cover = "/api/v1/packages/icon?id=" + url_encoded(p.id);
                games.push_back(std::move(g));
            }
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
    // the profile pictures are drawn at about 200 px: a larger texture keeps them sharp
    const bool large = want.id.rfind("avatarpick:", 0) == 0 ||
                       want.id.rfind("profilepic:", 0) == 0 || want.id.rfind("ownpic:", 0) == 0;
    std::vector<std::uint8_t> rgba =
        decode_cover(reinterpret_cast<const std::uint8_t *>(body.data()), body.size(),
                     large ? kLargeCoverSize : kCoverSize);
    if (rgba.empty())
        covers().fail(want.id);
    else
        covers().deliver(want.id, std::move(rgba));
}

void HttpSource::poll_packages(bool scan)
{
    int status = 0;
    std::string body;
    if (exchange(scan ? "POST" : "GET", scan ? "/api/v1/packages/scan" : "/api/v1/packages",
                 scan ? "{}" : "", &status, &body, 8) != Net::ok ||
        status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    PackageScan s;
    s.loaded = true;
    s.ever = flag(root, "ever");
    s.scanning = flag(root, "scanning");
    s.parsed = static_cast<int>(num(root, "parsed"));
    s.current = str(root, "current");
    std::vector<std::pair<std::string, std::string>> labels;
    std::vector<PackageDrive> drives;
    const cJSON *d = nullptr;
    cJSON_ArrayForEach(d, cJSON_GetObjectItemCaseSensitive(root, "drives"))
    {
        labels.emplace_back(str(d, "mount"), str(d, "label"));
        PackageDrive drive;
        drive.mount = str(d, "mount");
        drive.label = str(d, "label");
        drive.free_bytes = static_cast<std::uint64_t>(num(d, "free_bytes"));
        if (!drive.mount.empty())
            drives.push_back(std::move(drive));
    }
    std::vector<Package> list;
    const cJSON *p = nullptr;
    cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(root, "packages"))
    {
        Package k;
        k.id = str(p, "id");
        k.name = str(p, "name");
        k.file = str(p, "file");
        k.drive = str(p, "drive");
        for (const auto &[mount, label] : labels)
        {
            if (mount == k.drive)
                k.drive_label = label;
        }
        if (k.drive_label.empty())
            k.drive_label = k.drive;
        k.title_id = str(p, "title_id");
        k.content_id = str(p, "content_id");
        k.version = str(p, "version");
        k.kind = str(p, "kind");
        k.plat = static_cast<int>(num(p, "plat"));
        k.size = static_cast<std::uint64_t>(num(p, "size"));
        k.total = static_cast<std::uint64_t>(num(p, "total"));
        k.has_icon = flag(p, "has_icon");
        if (const cJSON *parts = cJSON_GetObjectItemCaseSensitive(p, "parts"))
        {
            k.parts_total = static_cast<int>(num(parts, "total"));
            k.parts_found = static_cast<int>(num(parts, "found"));
        }
        if (!k.id.empty())
            list.push_back(std::move(k));
    }
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.package_scan = s;
    data_.packages = std::move(list);
    data_.package_drives = std::move(drives);
    ++data_.serial;
}

// GET /api/v1/packages/job: the split that runs, or the last one.
void HttpSource::poll_package_split()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/packages/job", {}, &status, &body) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    PackageSplit j;
    j.state = str(root, "state");
    if (j.state.empty())
        j.state = "idle";
    j.active = flag(root, "active");
    j.can_cancel = flag(root, "can_cancel");
    j.phase = str(root, "phase");
    j.name = str(root, "name");
    j.parts = static_cast<int>(num(root, "parts"));
    j.part = static_cast<int>(num(root, "part"));
    j.percent = static_cast<int>(num(root, "percent"));
    j.error = str(root, "error");
    j.note = str(root, "note");
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.package_split = j;
    ++data_.serial;
}

void HttpSource::poll_package_install()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/packages/install/job", {}, &status, &body) != Net::ok ||
        status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    PackageInstall j;
    j.state = str(root, "state");
    j.active = flag(root, "active");
    j.can_cancel = flag(root, "can_cancel");
    j.cancelling = flag(root, "cancelling");
    j.name = str(root, "name");
    j.version = str(root, "version");
    j.error = str(root, "error");
    j.note = str(root, "note");
    j.bytes_total = num(root, "bytes_total");
    j.bytes_done = num(root, "bytes_done");
    j.percent = static_cast<int>(num(root, "percent", -1));
    j.remain_s = static_cast<int>(num(root, "remain_s", -1));
    if (j.state.empty())
        j.state = "idle";
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    const PackageInstall &o = data_.package_install;
    if (o.state == j.state && o.active == j.active && o.cancelling == j.cancelling &&
        o.name == j.name && o.percent == j.percent && o.bytes_done == j.bytes_done &&
        o.error == j.error && o.remain_s == j.remain_s)
        return;
    // A run that ended: the library may have a new game now.
    if (o.active && !j.active)
        library_wanted_ = true;
    data_.package_install = j;
    ++data_.serial;
}

void HttpSource::poll_package_queue()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/packages/queue", {}, &status, &body) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    PackageQueue q;
    q.running = flag(root, "running");
    q.pause_requested = flag(root, "pause_requested");
    q.halted = flag(root, "halted");
    q.halt_reason = str(root, "halt_reason");
    const cJSON *counts = cJSON_GetObjectItemCaseSensitive(root, "counts");
    q.waiting = static_cast<int>(num(counts, "waiting"));
    q.done = static_cast<int>(num(counts, "done"));
    q.failed =
        static_cast<int>(num(counts, "failed") + num(counts, "skipped") + num(counts, "cancelled"));
    const cJSON *it = nullptr;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "items"))
    {
        PackageQueueItem i;
        i.id = str(it, "id");
        i.name = str(it, "name");
        i.version = str(it, "version");
        i.kind = str(it, "kind");
        i.state = str(it, "state");
        i.message = str(it, "message");
        i.size = static_cast<std::uint64_t>(num(it, "size"));
        q.items.push_back(std::move(i));
    }
    cJSON_Delete(root);
    const auto key_of = [](const PackageQueue &x)
    {
        std::string key = std::to_string(x.running) + std::to_string(x.pause_requested) +
                          std::to_string(x.halted) + x.halt_reason;
        for (const PackageQueueItem &i : x.items)
            key += ';' + i.id + i.state + i.message;
        return key;
    };
    std::lock_guard<std::mutex> lock(mutex_);
    if (key_of(q) == key_of(data_.package_queue))
        return;
    data_.package_queue = std::move(q);
    ++data_.serial;
}

void HttpSource::poll_playtime()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/playtime", {}, &status, &body, 10) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    Playtime p;
    p.loaded = true;
    p.enabled =
        cJSON_GetObjectItemCaseSensitive(root, "enabled") == nullptr || flag(root, "enabled");
    p.now = static_cast<std::int64_t>(num(root, "now"));
    p.total = static_cast<int>(num(root, "total"));
    p.kept = static_cast<int>(num(root, "kept"));
    const cJSON *titles = cJSON_GetObjectItemCaseSensitive(root, "titles");
    const auto name_of = [&](const std::string &id)
    {
        const cJSON *n = cJSON_GetObjectItemCaseSensitive(titles, id.c_str());
        return cJSON_IsString(n) && n->valuestring != nullptr && n->valuestring[0]
                   ? std::string(n->valuestring)
                   : id;
    };
    const auto read = [&](const cJSON *o)
    {
        PlaySession s;
        s.id = str(o, "id");
        s.start = static_cast<std::int64_t>(num(o, "start"));
        s.end = static_cast<std::int64_t>(num(o, "end"));
        s.play = static_cast<int>(num(o, "play"));
        s.cpu_max = static_cast<int>(num(o, "cpu_max", -1));
        s.soc_max = static_cast<int>(num(o, "soc_max", -1));
        s.fan_avg = static_cast<int>(num(o, "fan_avg", -1));
        s.flags = static_cast<int>(num(o, "flags"));
        return s;
    };
    const cJSON *o = nullptr;
    cJSON_ArrayForEach(o, cJSON_GetObjectItemCaseSensitive(root, "sessions"))
    {
        PlaySession s = read(o);
        s.name = name_of(s.id);
        if (!s.id.empty())
            p.sessions.push_back(std::move(s));
    }
    const cJSON *live = cJSON_GetObjectItemCaseSensitive(root, "live");
    if (cJSON_IsObject(live))
    {
        p.live = true;
        p.current = read(live);
        p.current.name = str(live, "name");
        if (p.current.name.empty())
            p.current.name = name_of(p.current.id);
        p.live_state = str(live, "state");
    }
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.playtime = std::move(p);
    ++data_.serial;
}

namespace
{
SaveJob read_save_job(const cJSON *j)
{
    SaveJob x;
    x.active = flag(j, "active");
    x.can_cancel = flag(j, "can_cancel");
    x.state = str(j, "state");
    x.kind = str(j, "kind");
    x.phase = str(j, "phase");
    x.percent = static_cast<int>(num(j, "percent"));
    x.files_done = static_cast<int>(num(j, "files_done"));
    x.files_total = static_cast<int>(num(j, "files_total"));
    x.current = str(j, "current");
    x.error = str(j, "error");
    x.note = str(j, "note");
    x.path = str(j, "path");
    x.undo = str(j, "undo");
    x.ok_files = static_cast<int>(num(j, "ok_files"));
    x.elapsed_s = static_cast<int>(num(j, "elapsed_s"));
    x.finished_ago_s = static_cast<int>(num(j, "finished_ago_s", -1));
    return x;
}
} // namespace

void HttpSource::poll_saves()
{
    int status = 0;
    std::string body;
    const Net net = exchange("GET", "/api/v1/saves", {}, &status, &body, 20);
    cJSON *root = net == Net::ok && status == 200 ? cJSON_Parse(body.c_str()) : nullptr;
    Saves s;
    s.loaded = true;
    if (root == nullptr)
    {
        s.error = net == Net::ok ? message_of(body, "Die Spielstände ließen sich nicht lesen.")
                                 : "Die App hat nicht rechtzeitig geantwortet.";
        std::lock_guard<std::mutex> lock(mutex_);
        s.users = data_.saves.users;
        s.backups = data_.saves.backups;
        s.drives = data_.saves.drives;
        s.job = data_.saves.job;
        data_.saves = std::move(s);
        ++data_.serial;
        return;
    }
    s.game_running = flag(root, "game_running");
    s.game_id = str(root, "game_id");
    s.unfinished = static_cast<int>(num(root, "unfinished"));
    const cJSON *u = nullptr;
    cJSON_ArrayForEach(u, cJSON_GetObjectItemCaseSensitive(root, "users"))
    {
        SaveUser user;
        user.uid = str(u, "uid");
        user.name = str(u, "name");
        user.bytes = static_cast<std::uint64_t>(num(u, "bytes"));
        const cJSON *t = nullptr;
        cJSON_ArrayForEach(t, cJSON_GetObjectItemCaseSensitive(u, "titles"))
        {
            // apps never: their data is not a save (the web page leaves them out too)
            if (str(t, "kind") == "app")
                continue;
            SaveTitle x;
            x.uid = user.uid;
            x.id = str(t, "id");
            x.name = str(t, "name");
            x.platform = str(t, "platform");
            x.installed = flag(t, "installed");
            x.bytes = static_cast<std::uint64_t>(num(t, "bytes"));
            x.mtime = static_cast<std::int64_t>(num(t, "mtime"));
            user.titles.push_back(std::move(x));
        }
        s.users.push_back(std::move(user));
    }
    const cJSON *d = nullptr;
    cJSON_ArrayForEach(d, cJSON_GetObjectItemCaseSensitive(root, "drives"))
    {
        SaveDrive x;
        x.mount = str(d, "mount");
        x.label = str(d, "label");
        x.free_bytes = static_cast<std::uint64_t>(num(d, "free_bytes"));
        s.drives.push_back(std::move(x));
    }
    const cJSON *b = nullptr;
    cJSON_ArrayForEach(b, cJSON_GetObjectItemCaseSensitive(root, "backups"))
    {
        SaveBackup x;
        x.created = static_cast<std::int64_t>(num(b, "created"));
        x.path = str(b, "path");
        x.label = str(b, "label");
        x.kind = str(b, "kind");
        x.bytes = static_cast<std::uint64_t>(num(b, "bytes"));
        x.this_console = flag(b, "this_console");
        const cJSON *bu = nullptr;
        cJSON_ArrayForEach(bu, cJSON_GetObjectItemCaseSensitive(b, "users"))
        {
            x.user_names.push_back(str(bu, "name"));
        }
        const cJSON *t = nullptr;
        cJSON_ArrayForEach(t, cJSON_GetObjectItemCaseSensitive(b, "titles"))
        {
            SaveTitle y;
            y.uid = str(t, "uid");
            y.id = str(t, "id");
            y.name = str(t, "name");
            y.platform = str(t, "platform");
            y.bytes = static_cast<std::uint64_t>(num(t, "bytes"));
            x.titles.push_back(std::move(y));
        }
        s.backups.push_back(std::move(x));
    }
    std::sort(s.backups.begin(), s.backups.end(),
              [](const SaveBackup &a, const SaveBackup &c) { return a.created > c.created; });
    if (s.backups.size() > 40)
        s.backups.resize(40);
    s.job = read_save_job(cJSON_GetObjectItemCaseSensitive(root, "job"));
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.saves = std::move(s);
    ++data_.serial;
}

void HttpSource::poll_save_job()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/saves/job", {}, &status, &body, 6) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    SaveJob j = read_save_job(root);
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.saves.job = std::move(j);
    ++data_.serial;
}

void HttpSource::poll_game_backups()
{
    int status = 0;
    std::string body;
    const Net net = exchange("GET", "/api/v1/library/backups", {}, &status, &body, 30);
    cJSON *root = net == Net::ok && status == 200 ? cJSON_Parse(body.c_str()) : nullptr;
    GameBackups g;
    g.loaded = true;
    if (root == nullptr)
        g.error = net == Net::ok ? message_of(body, "Die Sicherungen ließen sich nicht lesen.")
                                 : "Die App hat nicht rechtzeitig geantwortet.";
    else
    {
        const cJSON *b = nullptr;
        cJSON_ArrayForEach(b, cJSON_GetObjectItemCaseSensitive(root, "backups"))
        {
            GameBackup x;
            x.path = str(b, "path");
            x.name = str(b, "name");
            x.drive = str(b, "drive");
            x.type = str(b, "type");
            x.size_bytes = static_cast<std::uint64_t>(num(b, "size_bytes"));
            x.modified = static_cast<std::int64_t>(num(b, "modified"));
            x.unfinished = flag(b, "unfinished");
            x.sums = flag(b, "sums");
            if (!x.path.empty())
                g.backups.push_back(std::move(x));
        }
        cJSON_Delete(root);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    data_.game_backups = std::move(g);
    ++data_.serial;
}

void HttpSource::poll_extras(const std::string &id)
{
    // the nine questions of the web app's window, in its order
    const std::string q = url_encoded(id);
    const std::string routes[9] = {"/api/v1/library/ampr/job",
                                   "/api/v1/library/image/job",
                                   "/api/v1/library/ampr?id=" + q,
                                   "/api/v1/library/image?id=" + q,
                                   "/api/v1/library/emu?id=" + q + "&lib=ampr",
                                   "/api/v1/library/emu?id=" + q + "&lib=playgo",
                                   "/api/v1/library/lz4?id=" + q,
                                   "/api/v1/library/trace?id=" + q,
                                   "/api/v1/library/plugins?id=" + q};
    std::string answers[9];
    bool lost[9] = {};
    for (int i = 0; i < 9; ++i)
    {
        int status = 0;
        std::string body;
        const Net net = exchange("GET", routes[i].c_str(), {}, &status, &body, 20);
        if (net == Net::ok && status == 200)
            answers[i] = std::move(body);
        else
            lost[i] = net != Net::ok; // a timeout is not an answer: for the same game the earlier one stays
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ExtrasState &e = data_.extras;
    const bool same = e.id == id;
    auto put = [&](std::string &dst, int i) {
        if (!(lost[i] && same))
            dst = std::move(answers[i]);
    };
    e.id = id;
    put(e.ampr_job, 0);
    put(e.image_job, 1);
    put(e.ampr, 2);
    put(e.image, 3);
    put(e.emu_ampr, 4);
    put(e.emu_playgo, 5);
    put(e.lz4, 6);
    put(e.trace, 7);
    put(e.plugins, 8);
    ++e.seq;
    ++data_.serial;
}

void HttpSource::poll_library_tools()
{
    int status = 0;
    std::string dup, body;
    if (exchange("GET", "/api/v1/library/duplicates", {}, &status, &body, 30) == Net::ok &&
        status == 200)
        dup = std::move(body);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.library_tools.duplicates = std::move(dup);
    ++data_.library_tools.seq;
    ++data_.serial;
}

void HttpSource::poll_klog_files()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/klog/files", {}, &status, &body, 6) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    std::vector<KlogFile> files;
    const cJSON *f = nullptr;
    cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(root, "files"))
    {
        KlogFile k;
        k.name = str(f, "name");
        k.kind = str(f, "kind");
        k.size = static_cast<std::uint64_t>(num(f, "size"));
        k.mtime = static_cast<std::int64_t>(num(f, "mtime"));
        k.active = flag(f, "active");
        if (!k.name.empty())
            files.push_back(std::move(k));
    }
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.klog_files = std::move(files);
    data_.klog_files_loaded = true;
    ++data_.serial;
}

// Pictures of one's own: JPG and PNG in /data/PS5-Cooling-Center/Profilbilder, at the root of every
// USB drive and in its Profilbilder folder (GET /api/v1/files/places and .../list).
void HttpSource::poll_own_pictures()
{
    int status = 0;
    std::string body;
    // a folder of their own, apart from the app's staging folder "avatar" (user, 31.10.2026); the
    // app makes it at its start
    std::vector<std::pair<std::string, std::string>> folders = {
        {"/data/PS5-Cooling-Center/Profilbilder", "Konsole"}};
    if (exchange("GET", "/api/v1/files/places", {}, &status, &body, 10) == Net::ok && status == 200)
    {
        if (cJSON *root = cJSON_Parse(body.c_str()))
        {
            const cJSON *p = nullptr;
            cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(root, "places"))
            {
                const std::string path = str(p, "path");
                if (path.rfind("/mnt/usb", 0) != 0 && path.rfind("/mnt/ext", 0) != 0)
                    continue;
                const std::string where = path.substr(path.find_last_of('/') + 1);
                folders.emplace_back(path, where);
                folders.emplace_back(path + "/Profilbilder", where);
            }
            cJSON_Delete(root);
        }
    }
    const auto picture = [](const std::string &name)
    {
        std::string low;
        for (const char c : name)
            low += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto ends = [&](const char *e)
        {
            const std::size_t n = std::strlen(e);
            return low.size() > n && low.compare(low.size() - n, n, e) == 0;
        };
        return ends(".jpg") || ends(".jpeg") || ends(".png");
    };
    std::vector<OwnPicture> found;
    for (const auto &[folder, where] : folders)
    {
        if (found.size() >= 60)
            break;
        const std::string route = "/api/v1/files/list?path=" + url_encoded(folder);
        if (exchange("GET", route.c_str(), {}, &status, &body, 10) != Net::ok || status != 200)
            continue;
        cJSON *root = cJSON_Parse(body.c_str());
        if (root == nullptr)
            continue;
        const cJSON *e = nullptr;
        cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "entries"))
        {
            const std::string name = str(e, "name");
            const auto size = static_cast<std::uint64_t>(num(e, "size"));
            if (str(e, "type") != "file" || !picture(name) || size == 0 || size > (16u << 20))
                continue;
            OwnPicture pic;
            pic.path = folder + "/" + name;
            pic.name = name;
            pic.where = where;
            pic.size = size;
            found.push_back(std::move(pic));
            if (found.size() >= 60)
                break;
        }
        cJSON_Delete(root);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    data_.own_pictures = std::move(found);
    data_.own_pictures_loaded = true;
    ++data_.serial;
}

// GET /api/v1/system for the user and the console; /avatars/index.json once.
void HttpSource::poll_profile()
{
    int status = 0;
    std::string body;
    ProfileInfo info;
    if (exchange("GET", "/api/v1/system", {}, &status, &body, 8) == Net::ok && status == 200)
    {
        if (cJSON *root = cJSON_Parse(body.c_str()))
        {
            info.loaded = true;
            info.console_name = str(root, "console_name");
            info.model = str(root, "model");
            if (const cJSON *u = cJSON_GetObjectItemCaseSensitive(root, "user"))
            {
                info.valid = flag(u, "valid");
                info.username = str(u, "username");
                info.uid_hex = str(u, "uid_hex");
                info.name_max = static_cast<int>(num(u, "name_max", 16));
                info.can_rename = flag(u, "can_rename");
            }
            cJSON_Delete(root);
        }
    }
    bool want_avatars = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        want_avatars = data_.avatars.empty();
    }
    std::vector<AvatarChoice> avatars;
    if (want_avatars && exchange("GET", "/avatars/index.json", {}, &status, &body, 6) == Net::ok &&
        status == 200)
    {
        if (cJSON *root = cJSON_Parse(body.c_str()))
        {
            const cJSON *item = nullptr;
            cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "images"))
            {
                AvatarChoice a;
                a.file = str(item, "file");
                a.label = str(item, "label");
                if (!a.file.empty())
                    avatars.push_back(std::move(a));
            }
            cJSON_Delete(root);
        }
    }
    AccountInfo account;
    if (exchange("GET", "/api/v1/profile/account/status", {}, &status, &body, 6) == Net::ok &&
        status == 200)
    {
        if (cJSON *root = cJSON_Parse(body.c_str()))
        {
            account.loaded = true;
            account.activated = flag(root, "activated");
            account.fakesignin = flag(root, "fakesignin");
            account.username = str(root, "username");
            const cJSON *a = nullptr;
            cJSON_ArrayForEach(a, cJSON_GetObjectItemCaseSensitive(root, "accounts"))
            {
                AccountSlot s;
                s.slot = static_cast<int>(num(a, "slot"));
                s.name = str(a, "name");
                s.active = flag(a, "active");
                if (!s.name.empty())
                    account.slots.push_back(std::move(s));
            }
            cJSON_Delete(root);
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (info.loaded)
        data_.profile = info;
    if (account.loaded)
        data_.account = std::move(account);
    if (!avatars.empty())
        data_.avatars = std::move(avatars);
    ++data_.serial;
}

void HttpSource::poll_klog_record()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/klog/record", {}, &status, &body, 6) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    KlogRecord r;
    r.loaded = true;
    r.recording = flag(root, "recording");
    r.file = str(root, "file");
    r.reason = str(root, "reason");
    r.bytes = static_cast<std::uint64_t>(num(root, "bytes"));
    r.max_bytes = static_cast<std::uint64_t>(num(root, "max_bytes"));
    r.lines = static_cast<int>(num(root, "lines"));
    r.elapsed_s = static_cast<int>(num(root, "elapsed_s"));
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.klog_record = std::move(r);
    ++data_.serial;
}

void HttpSource::poll_fs_places()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/files/places", {}, &status, &body, 10) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    std::vector<FsPlace> places;
    const cJSON *p = nullptr;
    cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(root, "places"))
    {
        FsPlace x;
        x.label = str(p, "label");
        x.path = str(p, "path");
        x.free_bytes = static_cast<std::int64_t>(num(p, "free_bytes", -1));
        x.writable = flag(p, "writable");
        places.push_back(std::move(x));
    }
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.fs_places = std::move(places);
    ++data_.serial;
}

void HttpSource::poll_fs_list(const std::string &path)
{
    FsList l;
    l.loaded = true;
    l.path = path;
    if (!path.empty())
    {
        int status = 0;
        std::string body;
        const std::string route = "/api/v1/files/list?path=" + url_encoded(path);
        const Net net = exchange("GET", route.c_str(), {}, &status, &body, 30);
        cJSON *root = net == Net::ok && status == 200 ? cJSON_Parse(body.c_str()) : nullptr;
        if (root == nullptr)
            l.error = net == Net::ok ? message_of(body, "Der Ordner lässt sich nicht lesen.")
                                     : "Die App hat nicht rechtzeitig geantwortet.";
        else
        {
            l.path = str(root, "path");
            l.parent = str(root, "parent");
            l.writable = flag(root, "writable");
            l.free_bytes = static_cast<std::int64_t>(num(root, "free_bytes", -1));
            l.total = static_cast<int>(num(root, "total"));
            l.truncated = flag(root, "truncated");
            const cJSON *e = nullptr;
            cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "entries"))
            {
                FsEntry x;
                x.name = str(e, "name");
                x.type = str(e, "type");
                x.size = static_cast<std::uint64_t>(num(e, "size"));
                x.mtime = static_cast<std::int64_t>(num(e, "mtime"));
                x.writable = flag(e, "writable");
                l.entries.push_back(std::move(x));
            }
            cJSON_Delete(root);
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    l.seq = data_.fs_list.seq + 1;
    data_.fs_list = std::move(l);
    ++data_.serial;
}

void HttpSource::poll_fs_job()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/files/job", {}, &status, &body, 6) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    FsJob j;
    j.active = flag(root, "active");
    j.state = str(root, "state");
    j.kind = str(root, "kind");
    j.dest = str(root, "dest");
    j.from = str(root, "from");
    j.items = static_cast<int>(num(root, "items"));
    j.items_done = static_cast<int>(num(root, "items_done"));
    j.current = str(root, "current");
    j.error = str(root, "error");
    j.note = str(root, "note");
    j.percent = static_cast<int>(num(root, "percent"));
    j.bytes_total = static_cast<std::uint64_t>(num(root, "bytes_total"));
    j.bytes_done = static_cast<std::uint64_t>(num(root, "bytes_done"));
    j.files_total = static_cast<int>(num(root, "files_total"));
    j.files_done = static_cast<int>(num(root, "files_done"));
    j.finished_ago_s = static_cast<int>(num(root, "finished_ago_s", -1));
    j.rate_bps = static_cast<std::uint64_t>(num(root, "rate_bps"));
    j.eta_s = static_cast<int>(num(root, "eta_s"));
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.fs_job = std::move(j);
    ++data_.serial;
}

// System's other answers: cooling over the weeks, the probes, the SoC channels, the controller, the
// drives (only while their probe is on: it reads SMART data), and the QR code now and then.
void HttpSource::poll_system_extra(bool first)
{
    int status = 0;
    std::string body;
    // One request that gets no answer ends the round: the rest would wait out their timeouts one
    // after another, and the worker would hold back temperatures and queued requests for half a
    // minute while the app hangs.
    bool down = false;
    const auto get = [&](const char *path, int timeout) -> std::string
    {
        if (down)
            return {};
        const Net net = exchange("GET", path, {}, &status, &body, timeout);
        if (net != Net::ok)
        {
            down = true;
            return {};
        }
        return status == 200 ? body : std::string();
    };
    std::string health = get("/api/v1/cooling-health", 4);
    std::string config = get("/api/v1/config", 4);
    std::string channels = get("/api/v1/channels", 6);
    std::string diag = get("/api/v1/controller/diag", 4);
    int mask = 0;
    if (cJSON *root = cJSON_Parse(config.c_str()))
    {
        mask = static_cast<int>(num(root, "probe_mask"));
        cJSON_Delete(root);
    }
    // the drives: "" while their probe is off; a failed reading keeps the last one instead of
    // showing none
    const bool drives_on = (mask & 16) != 0;
    std::string drives = drives_on ? get("/api/v1/drives", 8) : std::string();
    const bool keep_drives = drives_on && drives.empty();
    // The QR code at opening and then about once a minute (15 rounds of 4 s): the console's address
    // can change while the page stays open (a new DHCP lease), and the code must follow it.
    std::string qr;
    bool want_qr = first || ++qr_rounds_ >= 15;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        want_qr = want_qr || data_.system_data.qr.empty();
    }
    if (want_qr)
    {
        qr_rounds_ = 0;
        qr = get("/api/v1/qr", 4);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    SystemData &d = data_.system_data;
    d.status = status_raw_;
    if (!health.empty())
        d.health = std::move(health);
    if (!config.empty())
        d.config = std::move(config);
    if (!channels.empty())
        d.channels = std::move(channels);
    if (!diag.empty())
        d.diag = std::move(diag);
    if (!config.empty() &&
        !keep_drives) // without this round's config the probe's state is not known
        d.drives = std::move(drives);
    if (!qr.empty())
        d.qr = std::move(qr);
    ++d.seq;
    ++data_.serial;
}

void HttpSource::poll_storage()
{
    int status = 0;
    std::string body;
    std::vector<Volume> volumes;
    bool got = false;
    std::string system_raw;
    if (exchange("GET", "/api/v1/system", {}, &status, &body, 8) == Net::ok && status == 200)
    {
        system_raw = body;
        if (cJSON *root = cJSON_Parse(body.c_str()))
        {
            got = true;
            const cJSON *v = nullptr;
            cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "volumes"))
            {
                Volume x;
                x.label = str(v, "label");
                x.path = str(v, "path");
                x.total_bytes = static_cast<std::uint64_t>(num(v, "total_bytes"));
                x.used_bytes = static_cast<std::uint64_t>(num(v, "used_bytes"));
                x.free_bytes = static_cast<std::uint64_t>(num(v, "free_bytes"));
                x.ejectable = flag(v, "ejectable");
                volumes.push_back(std::move(x));
            }
            cJSON_Delete(root);
        }
    }
    UsbEject e;
    if (exchange("GET", "/api/v1/storage/eject", {}, &status, &body, 4) == Net::ok && status == 200)
    {
        if (cJSON *root = cJSON_Parse(body.c_str()))
        {
            e.loaded = true;
            e.available = flag(root, "available");
            e.running = flag(root, "running");
            e.state = str(root, "state");
            e.mount = str(root, "mount");
            e.message = str(root, "message");
            cJSON_Delete(root);
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (got)
    {
        data_.volumes = std::move(volumes);
        data_.volumes_loaded = true;
        data_.system_data.system = std::move(system_raw);
        ++data_.system_data.seq;
    }
    if (e.loaded)
        data_.usb_eject = std::move(e);
    ++data_.serial;
}

void HttpSource::poll_ps4mount()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/ps4mount", {}, &status, &body, 8) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    Ps4Mount m;
    m.loaded = true;
    m.enabled = flag(root, "enabled");
    m.running = flag(root, "running");
    m.busy = flag(root, "busy");
    m.standalone = str(root, "standalone");
    m.version = str(root, "version");
    const cJSON *cfg = cJSON_GetObjectItemCaseSensitive(root, "config");
    if (cJSON_GetObjectItemCaseSensitive(cfg, "batch_size") != nullptr)
    {
        m.config.valid = true;
        m.config.batch = flag(cfg, "batch");
        m.config.dlc = flag(cfg, "dlc");
        m.config.dlc_copy = flag(cfg, "dlc_copy");
        m.config.fast_reg = flag(cfg, "fast_reg");
        m.config.batch_size = static_cast<int>(num(cfg, "batch_size", 5));
        m.config.restore_batch = static_cast<int>(num(cfg, "restore_batch", 5));
        m.config.max_restore_batch = static_cast<int>(num(cfg, "max_restore_batch", 10));
        const cJSON *path = nullptr;
        cJSON_ArrayForEach(path, cJSON_GetObjectItemCaseSensitive(cfg, "paths"))
        {
            if (cJSON_IsString(path) && path->valuestring != nullptr)
                m.config.paths.emplace_back(path->valuestring);
        }
    }
    const cJSON *g = nullptr;
    cJSON_ArrayForEach(g, cJSON_GetObjectItemCaseSensitive(root, "games"))
    {
        Ps4Game x;
        x.title_id = str(g, "title_id");
        x.name = str(g, "name");
        x.version = str(g, "version");
        x.state = str(g, "state");
        x.mount = str(g, "mount");
        x.update_issue = str(g, "update_issue");
        x.has_patch = flag(g, "has_patch");
        x.size = static_cast<std::uint64_t>(num(g, "size"));
        x.dlc = static_cast<int>(num(g, "dlc"));
        x.dlc_total = static_cast<int>(num(g, "dlc_total"));
        x.updates = static_cast<int>(num(g, "updates"));
        if (!x.title_id.empty())
            m.games.push_back(std::move(x));
    }
    const cJSON *s = nullptr;
    cJSON_ArrayForEach(s, cJSON_GetObjectItemCaseSensitive(root, "skipped"))
    {
        Ps4Skipped x;
        x.path = str(s, "path");
        x.mount = str(s, "mount");
        x.title_id = str(s, "title_id");
        x.name = str(s, "name");
        x.why = str(s, "why");
        if (!x.path.empty())
            m.skipped.push_back(std::move(x));
    }
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    // Busy, PS4Mount answers from its last list or with none: an empty list then is not news, the
    // old one stays.
    if (m.busy && m.games.empty() && data_.ps4mount.loaded)
    {
        data_.ps4mount.busy = true;
        ++data_.serial;
        return;
    }
    if (!m.config.valid && data_.ps4mount.config.valid)
        m.config = data_.ps4mount.config;
    data_.ps4mount = std::move(m);
    ++data_.serial;
}

void HttpSource::fetch_ps4_detail(const std::string &id)
{
    Ps4GameDetail d;
    d.id = id;
    d.loaded = true;
    int status = 0;
    std::string body;
    const std::string path = "/api/v1/ps4mount/game?id=" + url_encoded(id);
    if (exchange("GET", path.c_str(), {}, &status, &body, 8) != Net::ok || status != 200)
    {
        d.error = message_of(body, "Die App hat nicht geantwortet.");
    }
    else if (cJSON *root = cJSON_Parse(body.c_str()))
    {
        d.dlc_limit = static_cast<int>(num(root, "dlc_limit", 29));
        const cJSON *u = nullptr;
        cJSON_ArrayForEach(u, cJSON_GetObjectItemCaseSensitive(root, "updates"))
        {
            Ps4Update x;
            x.file = str(u, "file");
            x.version = str(u, "version");
            x.name = str(u, "name");
            x.active = flag(u, "active");
            d.updates.push_back(std::move(x));
        }
        const cJSON *c = nullptr;
        cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(root, "dlcs"))
        {
            Ps4Dlc x;
            x.content_id = str(c, "content_id");
            x.name = str(c, "name");
            x.on = flag(c, "on");
            x.data = flag(c, "data");
            x.copy = str(c, "copy");
            x.reason = str(c, "reason");
            d.dlcs.push_back(std::move(x));
        }
        cJSON_Delete(root);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    d.seq = data_.ps4_detail.seq + 1;
    data_.ps4_detail = std::move(d);
    ++data_.serial;
}

void HttpSource::fetch_ps4_log()
{
    int status = 0;
    std::string body;
    std::string log;
    if (exchange("GET", "/api/v1/ps4mount/log", {}, &status, &body, 8) == Net::ok && status == 200)
    {
        cJSON *root = cJSON_Parse(body.c_str());
        log = root != nullptr ? str(root, "log") : "";
        cJSON_Delete(root);
    }
    else
    {
        log = message_of(body, "Das Protokoll ließ sich nicht lesen.");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    data_.ps4_log = log.empty() ? "(leer)" : log;
    ++data_.ps4_log_seq;
    ++data_.serial;
}

void HttpSource::poll_disc()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/disc", {}, &status, &body, 6) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    DiscState d;
    d.loaded = true;
    const cJSON *has = cJSON_GetObjectItemCaseSensitive(root, "has_drive");
    d.has_drive = !cJSON_IsFalse(has);
    d.enabled = flag(root, "enabled");
    d.auto_dump = flag(root, "auto_dump");
    d.drive = str(root, "drive");
    d.present = flag(root, "present");
    d.has_title = flag(root, "has_title");
    d.dumpable = flag(root, "dumpable");
    d.type = str(root, "type");
    d.serial = str(root, "serial");
    d.name = str(root, "name");
    d.title_state = str(root, "title_state");
    d.image_bytes = static_cast<std::uint64_t>(num(root, "image_bytes"));
    const cJSON *job = cJSON_GetObjectItemCaseSensitive(root, "job");
    d.job_state = str(job, "state");
    if (d.job_state.empty())
        d.job_state = "idle";
    d.job_name = str(job, "name");
    d.job_error = str(job, "error");
    d.job_note = str(job, "note");
    d.job_done = num(job, "done");
    d.job_total = num(job, "total");
    d.job_eta_s = static_cast<int>(num(job, "eta_s"));
    const cJSON *e = nullptr;
    cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(root, "events"))
        d.events.push_back(str(e, "text"));
    const cJSON *x = nullptr;
    cJSON_ArrayForEach(x, cJSON_GetObjectItemCaseSensitive(root, "dumps"))
    {
        DiscImage i;
        i.name = str(x, "name");
        i.serial = str(x, "serial");
        i.crc32 = str(x, "crc32");
        i.size = static_cast<std::uint64_t>(num(x, "size"));
        i.verified = flag(x, "verified");
        d.images.push_back(std::move(i));
    }
    d.dir = str(root, "dir");
    d.redump = flag(cJSON_GetObjectItemCaseSensitive(root, "redump"), "usable");
    cJSON_Delete(root);
    const auto key_of = [](const DiscState &s)
    {
        std::string k = std::to_string(s.has_drive) + s.drive + std::to_string(s.enabled) +
                        std::to_string(s.auto_dump) + std::to_string(s.present) + s.type +
                        s.serial + s.name + s.title_state + s.job_state +
                        std::to_string(static_cast<long long>(s.job_done)) + s.job_error +
                        std::to_string(s.events.size()) + std::to_string(s.images.size());
        for (const DiscImage &i : s.images)
            k += i.name;
        return k;
    };
    std::lock_guard<std::mutex> lock(mutex_);
    if (data_.disc.loaded && key_of(d) == key_of(data_.disc))
        return;
    data_.disc = std::move(d);
    ++data_.serial;
}

void HttpSource::poll_dump()
{
    int status = 0;
    std::string body;
    if (exchange("GET", "/api/v1/dump", {}, &status, &body, 6) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    DumpState d;
    d.loaded = true;
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(root, "settings");
    if (cJSON_IsObject(s))
    {
        DumpSettings &o = d.settings;
        o.valid = true;
        o.target = str(s, "target");
        o.subdir = str(s, "subdir");
        o.folder_titles = flag(s, "folder_titles");
        o.dump_dlc = flag(s, "dump_dlc");
        o.verify = flag(s, "verify");
        o.decrypt = flag(s, "decrypt");
        o.fself = flag(s, "fself");
        o.backport = flag(s, "backport");
        o.decrypt_supported =
            !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(s, "decrypt_supported"));
        o.ps4_split = static_cast<int>(num(s, "ps4_split", 3));
        o.ps4_backport_level = static_cast<int>(num(s, "ps4_backport_level", 4));
        o.ps5_backport_level = static_cast<int>(num(s, "ps5_backport_level", 1));
    }
    const cJSON *t = nullptr;
    cJSON_ArrayForEach(t, cJSON_GetObjectItemCaseSensitive(root, "targets"))
    {
        DumpTarget x;
        x.mount = str(t, "mount");
        x.label = str(t, "label");
        x.fs = str(t, "fs");
        x.free_bytes = static_cast<std::uint64_t>(num(t, "free_bytes"));
        x.console = flag(t, "console");
        d.targets.push_back(std::move(x));
    }
    const cJSON *q = cJSON_GetObjectItemCaseSensitive(root, "queue");
    d.queue.active = flag(q, "active");
    d.queue.current = static_cast<int>(num(q, "current", -1));
    d.queue.delay = static_cast<int>(num(q, "delay", 30));
    d.queue.wait = static_cast<int>(num(q, "wait"));
    d.queue.max = static_cast<int>(num(q, "max", 64));
    d.queue.replace = flag(q, "replace");
    d.queue.interrupted = flag(q, "interrupted");
    const cJSON *it = nullptr;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(q, "items"))
    {
        DumpQueueItem x;
        x.id = str(it, "id");
        x.name = str(it, "name");
        x.state = str(it, "state");
        x.message = str(it, "message");
        x.disc_out = flag(it, "disc") && !flag(it, "disc_in");
        x.custom = flag(it, "custom");
        x.dumped = flag(it, "dumped");
        d.queue.items.push_back(std::move(x));
    }
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(root, "job");
    DumpJob &o = d.job;
    o.state = str(j, "state");
    if (o.state.empty())
        o.state = "idle";
    o.active = flag(j, "active");
    o.verify = flag(j, "verify");
    o.name = str(j, "name");
    o.title_id = str(j, "title_id");
    o.stage = str(j, "stage");
    o.kind = str(j, "kind");
    o.drive = str(j, "drive");
    o.dest = str(j, "dest");
    o.folder = str(j, "folder");
    o.error = str(j, "error");
    o.note = str(j, "note");
    o.done_bytes = num(j, "done_bytes");
    o.total_bytes = num(j, "total_bytes");
    o.checked_bytes = num(j, "checked_bytes");
    o.rate = num(j, "rate");
    o.eta_s = static_cast<int>(num(j, "eta_s"));
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    // A dump or a queue that just ended: the library knows the game as dumped now, the list of
    // dumps has one more.
    const bool was = data_.dump.job.active || data_.dump.queue.active;
    if (was && !d.job.active && !d.queue.active)
        library_wanted_ = true;
    data_.dump = std::move(d);
    ++data_.serial;
}

void HttpSource::poll_dumps(bool scan)
{
    int status = 0;
    std::string body;
    if (exchange(scan ? "POST" : "GET", scan ? "/api/v1/dumps/scan" : "/api/v1/dumps",
                 scan ? "{}" : "", &status, &body, 8) != Net::ok ||
        status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    DumpList l;
    l.loaded = true;
    l.scanning = flag(root, "scanning");
    l.ever = flag(root, "ever");
    const cJSON *x = nullptr;
    cJSON_ArrayForEach(x, cJSON_GetObjectItemCaseSensitive(root, "dumps"))
    {
        DumpEntry e;
        e.mount = str(x, "mount");
        e.dir = str(x, "dir");
        e.folder = str(x, "folder");
        e.title_id = str(x, "title_id");
        e.title = str(x, "title");
        e.state = str(x, "state");
        e.kind = str(x, "kind");
        const double measured = num(x, "measured", -1.0);
        e.bytes = static_cast<std::uint64_t>(measured >= 0.0 ? measured : num(x, "bytes"));
        e.decrypt = flag(x, "decrypt");
        e.fself = flag(x, "fself");
        e.backport = flag(x, "backport");
        l.dumps.push_back(std::move(e));
    }
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(mutex_);
    data_.dumps = std::move(l);
    ++data_.serial;
}

void HttpSource::poll_backdrops()
{
    int status = 0;
    std::string body;
    // An app without the route (older than 29.10.2026) answers 404: every area keeps the tile's own
    // picture.
    if (exchange("GET", "/api/v1/backdrops", {}, &status, &body) != Net::ok || status != 200)
        return;
    cJSON *root = cJSON_Parse(body.c_str());
    if (root == nullptr)
        return;
    std::vector<BackdropSetting> list;
    const cJSON *areas = cJSON_GetObjectItemCaseSensitive(root, "areas");
    const cJSON *a = nullptr;
    cJSON_ArrayForEach(a, areas)
    {
        BackdropSetting b;
        b.id = str(a, "id");
        b.label = str(a, "label");
        const std::string mode = str(a, "mode");
        b.mode = mode == "custom" ? BackdropMode::custom
                 : mode == "off"  ? BackdropMode::off
                                  : BackdropMode::standard;
        b.custom = flag(a, "custom");
        b.stamp = str(a, "stamp");
        if (backdrop_area(b.id) >= 0)
            list.push_back(std::move(b));
    }
    cJSON_Delete(root);
    backdrops().apply(list);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto same = [](const BackdropSetting &x, const BackdropSetting &y)
    {
        return x.id == y.id && x.mode == y.mode && x.custom == y.custom && x.stamp == y.stamp &&
               x.label == y.label;
    };
    if (data_.backdrops_loaded &&
        std::equal(list.begin(), list.end(), data_.backdrops.begin(), data_.backdrops.end(), same))
        return;
    data_.backdrops = std::move(list);
    data_.backdrops_loaded = true;
    ++data_.serial;
}

void HttpSource::fetch_backdrop()
{
    const BackdropWant want = backdrops().wanted_custom();
    if (want.area < 0)
        return;
    const std::string path =
        std::string("/api/v1/backdrops/image?area=") + backdrop_name(want.area);
    int status = 0;
    std::string body;
    if (exchange("GET", path.c_str(), {}, &status, &body, 8) != Net::ok || status != 200)
    {
        backdrops().fail_custom(want.area, want.stamp);
        return;
    }
    std::vector<std::uint8_t> rgba =
        decode_backdrop(reinterpret_cast<const std::uint8_t *>(body.data()), body.size());
    sys::log("[PS5CC] backdrop %s %zu bytes ok=%d", backdrop_name(want.area), body.size(),
             rgba.empty() ? 0 : 1);
    if (rgba.empty())
        backdrops().fail_custom(want.area, want.stamp);
    else
        backdrops().deliver_custom(want.area, want.stamp, std::move(rgba));
}

void HttpSource::poll_jobs()
{
    static const char *const kRoutes[] = {"/api/v1/library/copy", "/api/v1/library/convert",
                                          "/api/v1/library/image/job", "/api/v1/library/storage",
                                          "/api/v1/library/delete"};
    std::array<Job, static_cast<std::size_t>(JobKind::count)> jobs{};
    std::array<Job, static_cast<std::size_t>(JobKind::count)> before;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        before = data_.jobs;
    }
    for (std::size_t i = 0; i < jobs.size(); ++i)
    {
        int status = 0;
        std::string body;
        const Net net = exchange("GET", kRoutes[i], {}, &status, &body);
        // A timeout is not an answer: the card keeps what it showed (otherwise one slow reply under load would show "no job"
        // while a copy runs, and would start a library reload because the job seems to have ended).
        if (net != Net::ok)
        {
            jobs[i] = before[i];
            continue;
        }
        // Without ShadowMountPlus its route answers 503: the card simply stays empty.
        if (status != 200)
            continue;
        cJSON *root = cJSON_Parse(body.c_str());
        if (root == nullptr)
        {
            jobs[i] = before[i];
            continue;
        }
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
        job.active = !idle && (flag(root, "active") || job.state == "running" ||
                               job.state == "scanning" || job.state == "verifying");
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
                job.name = kind == "check"    ? "Prüfen"
                           : kind == "unpack" ? "Entpacken"
                           : kind == "repair" ? "Reparieren"
                                              : kind;
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
    job.state = active                 ? state
                : state == "completed" ? "done"
                : state == "failed"    ? "failed"
                                       : "cancelled";
    std::string tid = str(root, "title_id");
    job.name = !tid.empty() ? game_name(tid) : str(root, "title_name");
    if (job.name.empty())
        job.name = str(root, "name");
    const std::string op = str(root, "operation");
    const std::string verb = op == "move"      ? "Verschieben"
                             : op == "unpack"  ? "Entpacken"
                             : op == "install" ? "Installieren"
                                               : op;
    const std::string dest = str(root, "destination");
    job.detail = verb + (dest.empty() ? "" : " → " + dest);
    job.total = num(root, "total_bytes");
    job.done = num(root, "processed_bytes");
    job.error = str(root, "result_error");
    job.cancellable = flag(root, "cancellable") && !flag(root, "cancel_requested");
    // As for a delete: ShadowMountPlus up to 1.7beta5fix1 refused the job while this app runs; the
    // app starts it again once this app has ended, so it ends itself (once per wait).
    const bool waits =
        active && str(root, "waiting_for") == "tile" && str(root, "tile") == own_title_id();
    if (waits)
    {
        job.detail = str(root, "phase");
        if (!leaving_for_storage_)
            notice(Notice::Kind::leaving, "„" + job.name + "“: " + verb,
                   "Die Vollbild-App schließt sich dafür. Das Ergebnis kommt als Meldung.");
    }
    leaving_for_storage_ = waits;
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
    // ShadowMountPlus up to 1.7beta5fix1 refuses every uninstall while any title it mounted runs,
    // this app included: the delete waits until this app has ended, so it ends itself (once per
    // job; the app reports the result as a notification).
    if (job.active && str(root, "waiting_for") == "tile" && str(root, "tile") == own_title_id() &&
        !leaving_for_delete_)
    {
        leaving_for_delete_ = true;
        notice(Notice::Kind::leaving, "„" + job.name + "“ wird gelöscht",
               "Die Vollbild-App schließt sich dafür. Das Ergebnis kommt als Meldung.");
    }
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
