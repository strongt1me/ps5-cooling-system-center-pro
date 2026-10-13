/*
 * Starter app of "PS5 Cooling & System Center - Pro" (FAKE08086; a dump folder for /data/homebrew, see tools/make_starter.sh).
 *
 * A tap on its tile runs this program. If the app is already up (127.0.0.1:8086 answers), it asks it to open the web UI in the
 * console's browser (POST /api/v1/open-ui). Otherwise it sends the app's ELF, which is part of this very eboot.bin (.incbin below),
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

static void run(void) {
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
