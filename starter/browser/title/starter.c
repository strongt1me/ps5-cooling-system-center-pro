/*
 * Starter app of "PS5 Cooling & System Center - Pro" (FAKE08086; a dump folder for /data/homebrew, see tools/make_starter.sh).
 *
 * A tap on its tile runs this program. If the app is already up (127.0.0.1:8086 answers), it asks it to open the web UI in the
 * console's browser (POST /api/v1/open-ui). An app that is OLDER than the one in this eboot.bin (compared by the version the app names
 * in /api/v1/system and the mark "PS5CC-VERSION:<version>:" in the ELF) and not busy with a job is replaced first (07.11.2026): the new
 * app retires the old one when it starts. Otherwise it sends the app's ELF, which is part of this very eboot.bin (.incbin below),
 * to the payload loader of the jailbreak (127.0.0.1:9021) — the same as sending the ELF by hand —, waits until the app answers and then asks for
 * the page. After that it ends.
 *
 * Built as a native PS5 title (starter/build_title.sh): the console's app loader wants a real process-parameter block and a
 * matching signature; a plain ELF does not start. Only functions of libkernel and libSceSystemService are used, no C library
 * (hence -ffreestanding, no memset/memcpy, and sce_module/libc.prx in the folder only because the loader insists on it).
 * The method — freestanding starter, linker script, ps5-native-tool, sce_module/libc.prx — follows ps5-native-app-boilerplate
 * (BlackBearReloaded, GPL-3.0-or-later) and my USB formatter's starter, which runs on FW 12.00.
 */
#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#define PORT_UI     8086
#define PORT_LOADER 9021

/* libkernel / libSceSystemService (stubs in target/lib of the SDK) */
int close(int fd);
int sceKernelUsleep(unsigned int microseconds);
int sceKernelDebugOutText(int channel, const char *text);
int sceKernelSendNotificationRequest(int device, void *request, size_t size, int blocking);
int sceSystemServiceLoadExec(const char *path, const char *const *argv);

__asm__(".section .rodata\n"
        ".global payload_elf\n"
        "payload_elf:\n"
        ".incbin \"payload.elf\"\n"
        ".global payload_elf_end\n"
        "payload_elf_end:\n"
        ".previous\n");
extern const unsigned char payload_elf[], payload_elf_end[];

/* from the linker script (title.ld); the loader of the console does not clear BSS */
extern char __bss_start[], __bss_end[];

static struct {
    char reserved[45];
    char message[3075];
} notify_request;

static size_t length(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Message in the kernel log and as a notification on the screen */
static void report(const char *msg) {
    volatile char *dst = notify_request.message;
    size_t i = 0;
    for (; msg[i] && i + 1 < sizeof(notify_request.message); i++) dst[i] = msg[i];
    dst[i] = 0;
    sceKernelDebugOutText(0, msg);
    sceKernelDebugOutText(0, "\n");
    sceKernelSendNotificationRequest(0, &notify_request, sizeof(notify_request), 0);
}

static int connect_local(int port) {
    struct sockaddr_in sa;
    volatile unsigned char *p = (volatile unsigned char *)&sa;
    for (size_t i = 0; i < sizeof(sa); i++) p[i] = 0;
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    if (connect(s, (const struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(s);
        return -1;
    }
    return s;
}

static int send_all(int s, const unsigned char *p, size_t n);

/* ---- the version of the app that runs against the one this tile carries ---- */

static size_t find(const char *hay, size_t hn, const char *needle) {
    size_t nn = length(needle);
    if (nn == 0 || hn < nn) return (size_t)-1;
    for (size_t i = 0; i + nn <= hn; i++) {
        if (hay[i] != needle[0]) continue;
        size_t j = 1;
        while (j < nn && hay[i + j] == needle[j]) j++;
        if (j == nn) return i;
    }
    return (size_t)-1;
}

/* "1.77.1" -> v[0..3]; 0 for anything that is not a version */
static int parse_version(const char *s, long v[4]) {
    for (int i = 0; i < 4; i++) v[i] = 0;
    int part = 0, digits = 0;
    for (size_t i = 0; s[i]; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            v[part] = v[part] * 10 + (c - '0');
            digits = 1;
            if (v[part] > 100000) return 0;
        } else if (c == '.' && digits && part < 3) {
            part++;
            digits = 0;
        } else {
            return 0;
        }
    }
    return digits;
}

/* -1 a older than b, 0 equal, 1 newer, 2 when one is not a version */
static int compare_versions(const char *a, const char *b) {
    long x[4], y[4];
    if (!parse_version(a, x) || !parse_version(b, y)) return 2;
    for (int i = 0; i < 4; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

/* the version of the ELF in this eboot.bin: the text after "PS5CC-VERSION:" up to the next ":"; 0 when there is no such mark */
static int embedded_version(char *out, size_t cap) {
    static const char mark[] = "PS5CC-VERSION:";
    const size_t mlen = sizeof(mark) - 1;
    const char *p = (const char *)payload_elf;
    size_t n = (size_t)(payload_elf_end - payload_elf);
    size_t at = 0;
    while (at < n) {
        size_t f = find(p + at, n - at, mark);
        if (f == (size_t)-1) return 0;
        const char *v = p + at + f + mlen;
        size_t left = n - (at + f + mlen), k = 0;
        while (k < left && v[k] != ':' && k + 1 < cap) {
            out[k] = v[k];
            k++;
        }
        out[k] = 0;
        long dummy[4];
        if (k > 0 && k < left && v[k] == ':' && parse_version(out, dummy)) return 1;
        at += f + mlen;
    }
    return 0;
}

/* GET path from the app: the whole answer in buf (NUL-terminated), its length, or -1 when the app does not answer */
static int connect_local(int port);
static int http_get(const char *path, char *buf, size_t cap) {
    static const char head[] = "GET ";
    static const char tail[] = " HTTP/1.1\r\nHost: 127.0.0.1:8086\r\nConnection: close\r\n\r\n";
    char req[160];
    size_t n = 0;
    for (size_t i = 0; head[i]; i++) req[n++] = head[i];
    for (size_t i = 0; path[i] && n + sizeof(tail) + 1 < sizeof(req); i++) req[n++] = path[i];
    for (size_t i = 0; tail[i]; i++) req[n++] = tail[i];
    int s = connect_local(PORT_UI);
    if (s < 0) return -1;
    struct timeval tv = { 5, 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    size_t got = 0;
    if (send_all(s, (const unsigned char *)req, n) == 0) {
        for (;;) {
            ssize_t r = recv(s, buf + got, cap - 1 - got, 0);
            if (r <= 0) break;
            got += (size_t)r;
            if (got + 1 >= cap) break;
        }
    }
    close(s);
    buf[got] = 0;
    return (int)got;
}

static int answered_ok(const char *buf, int n) {
    static const char ok[] = "HTTP/1.1 200";
    if (n < (int)sizeof(ok) - 1) return 0;
    for (size_t i = 0; ok[i]; i++)
        if (buf[i] != ok[i]) return 0;
    return 1;
}

/* the value of a JSON string after key (`"app_version":"`) in the answer; 0 when it is not there */
static int json_string(const char *buf, int n, const char *key, char *out, size_t cap) {
    size_t f = find(buf, (size_t)n, key);
    if (f == (size_t)-1) return 0;
    const char *v = buf + f + length(key);
    size_t k = 0;
    while (v[k] && v[k] != '"' && k + 1 < cap) {
        out[k] = v[k];
        k++;
    }
    out[k] = 0;
    return k > 0 && v[k] == '"';
}

static int same(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static int send_all(int s, const unsigned char *p, size_t n) {
    while (n) {
        ssize_t w = send(s, p, n, 0);
        if (w <= 0) return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* POST /api/v1/open-ui: 1 = the app answered, 0 = it is not running */
static int open_ui(void) {
    static const char req[] = "POST /api/v1/open-ui HTTP/1.1\r\nHost: 127.0.0.1:8086\r\n"
                              "Content-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
    char buf[256];
    int s = connect_local(PORT_UI);
    if (s < 0) return 0;
    /* an app that accepts the connection but never answers (stuck) must not hold the starter for good */
    struct timeval tv = { 5, 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    send_all(s, (const unsigned char *)req, length(req));
    while (recv(s, buf, sizeof(buf), 0) > 0) {}
    close(s);
    return 1;
}

/* the embedded ELF to the loader; 0 = sent */
static int send_payload(void) {
    int s = connect_local(PORT_LOADER);
    if (s < 0) {
        report("PS5 Cooling Center: ELF-Loader (Port 9021) nicht erreichbar - ist der Jailbreak geladen?");
        return -1;
    }
    int rc = send_all(s, payload_elf, (size_t)(payload_elf_end - payload_elf));
    close(s);
    if (rc != 0) report("PS5 Cooling Center: Senden an den ELF-Loader fehlgeschlagen");
    return rc;
}

/* One job after the other: does the app of this console run one right now? (the routes the web app shows jobs of) */
static int app_busy(void) {
    static const char *const routes[] = { "/api/v1/library/image/job", "/api/v1/files/job", "/api/v1/library/ampr/job",
                                          "/api/v1/library/copy", "/api/v1/library/convert", "/api/v1/library/delete",
                                          "/api/v1/packages/install/job", "/api/v1/saves/job" };
    static char body[2048];
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        int n = http_get(routes[i], body, sizeof(body));
        if (n > 0 && answered_ok(body, n) && find(body, (size_t)n, "\"active\":true") != (size_t)-1) return 1;
    }
    return 0;
}

/* An app answers: is it older than the one in this eboot.bin, and idle? Then ours goes over it (the new app retires the old one),
 * and the tile waits until the new version answers. A newer or equal app, one without a version and one with a job stay. */
static void maybe_update(void) {
    static char body[2048];
    char mine[24], running[24];
    int n = http_get("/api/v1/system", body, sizeof(body));
    if (n <= 0 || !answered_ok(body, n)) return;
    if (!embedded_version(mine, sizeof(mine))) return;
    if (!json_string(body, n, "\"app_version\":\"", running, sizeof(running))) return;
    if (compare_versions(running, mine) != -1) return;
    if (app_busy()) {
        report("PS5 Cooling Center: Eine neuere App liegt bereit, aber ein Auftrag laeuft noch. Danach die Kachel noch einmal oeffnen.");
        return;
    }
    report("PS5 Cooling Center: Die App wird aktualisiert ...");
    if (send_payload() != 0) return;
    for (int i = 0; i < 120; i++) {  /* up to 30 s */
        sceKernelUsleep(250 * 1000);
        n = http_get("/api/v1/system", body, sizeof(body));
        if (n > 0 && answered_ok(body, n) && json_string(body, n, "\"app_version\":\"", running, sizeof(running)) &&
            same(running, mine))
            return;
    }
}

static void run(void) {
    maybe_update();
    if (open_ui()) return;
    if (send_payload() != 0) return;
    for (int i = 0; i < 120; i++) {  /* up to 30 s */
        sceKernelUsleep(250 * 1000);
        if (open_ui()) return;
    }
    report("PS5 Cooling Center: Die App antwortet nicht");
}

/* Entry of the title. It ends with sceSystemServiceLoadExec("exit") (see the USB formatter's starter: exit() raises SIGSYS in a
 * native title, and closing the own app by sceSystemServiceKillApp is refused for normal apps). Before that a wait: when the title
 * ended at the very moment the browser was asked for, its process started in the middle of the teardown and the sandbox of the
 * title could stay behind (measured with the formatter, 12.10.2026). */
__attribute__((noreturn, visibility("default"))) void _start(void *process_parameters, void (*teardown)(void)) {
    (void)process_parameters;
    (void)teardown;
    for (volatile char *p = __bss_start; p < __bss_end; p++) *p = 0;
    run();
    sceKernelUsleep(3000 * 1000);
    sceSystemServiceLoadExec("exit", NULL);
    for (;;) sceKernelUsleep(100 * 1000);
}
