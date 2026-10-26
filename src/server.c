/*
 * A small HTTP server for the demonstration UI.
 *
 *   GET  /            the page (read from ui/index.html)
 *   POST /compile     body is AeroDSL source, reply is the JSON phase dump
 *
 * Single-threaded, one request at a time, loopback only. It exists to make
 * the compiler's phases visible during a demonstration and is not a web
 * server in any serious sense. The compiler itself does not depend on it.
 */
#include <errno.h>
#include <signal.h>

/* Sockets: POSIX on Linux and macOS, Winsock on Windows. The Windows headers
 * come first, with their TokenType renamed (see src/port.c). */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define TokenType Win32_TokenType
#include <winsock2.h>
#include <ws2tcpip.h>
#undef TokenType
typedef SOCKET sock_t;
#define SOCK_BAD INVALID_SOCKET
#define sock_close closesocket
#define sock_read(fd, buf, n) recv((fd), (buf), (int)(n), 0)
#define sock_write(fd, buf, n) send((fd), (buf), (int)(n), 0)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_BAD (-1)
#define sock_close close
#define sock_read read
#define sock_write write
#endif

#include "aerodsl.h"

#define MAX_BODY (1 << 20)

static const char *const UI_PATHS[] = {"ui/index.html", "../ui/index.html",
                                       "./AeroDSL/ui/index.html"};

static char *load_ui(const char **found) {
    for (int i = 0; i < LEN(UI_PATHS); i++) {
        FILE *f = fopen(UI_PATHS[i], "r");
        if (!f) continue;
        char *html = read_all(f);
        fclose(f);
        *found = UI_PATHS[i];
        return html;
    }
    return NULL;
}

static void send_all(sock_t fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        long n = (long)sock_write(fd, data + sent, len - sent);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return;
        }
        sent += (size_t)n;
    }
}

static void send_response(sock_t fd, const char *status, const char *type, const char *body,
                          size_t len) {
    char head[256];
    int n = snprintf(head, sizeof head,
                     "HTTP/1.1 %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: close\r\n\r\n",
                     status, type, len);
    send_all(fd, head, (size_t)n);
    if (len) send_all(fd, body, len);
}

/* Reads a whole request: headers, then Content-Length bytes of body. */
static char *read_request(sock_t fd, char **body_out) {
    size_t cap = 8192, len = 0;
    char *buf = xmalloc(cap);
    char *header_end = NULL;

    for (;;) {
        if (len + 1 >= cap) {
            if (cap >= MAX_BODY * 2) break;
            buf = xrealloc(buf, cap *= 2);
        }
        long n = (long)sock_read(fd, buf + len, cap - len - 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        len += (size_t)n;
        buf[len] = '\0';

        if (!header_end) header_end = strstr(buf, "\r\n\r\n");
        if (!header_end) continue;

        long want = 0;
        const char *cl = str_find_ci(buf, "Content-Length:");
        if (cl && cl < header_end) want = strtol(cl + 15, NULL, 10);
        size_t have = len - (size_t)(header_end + 4 - buf);
        if (want <= 0 || have >= (size_t)want) break;
    }

    buf[len] = '\0';
    *body_out = header_end ? header_end + 4 : buf + len;
    return buf;
}

static void handle_compile(sock_t fd, const char *source) {
    Unit u;
    unit_compile(&u, source, options_default());

    diag_sort(&u.diags);

    char *json = NULL;
    size_t jlen = 0;
    MemOut m;
    FILE *mem = memout_open(&m);
    if (mem) {
        json_dump(mem, &u, source);
        json = memout_close(&m, &jlen);
    }
    unit_free(&u);

    if (json)
        send_response(fd, "200 OK", "application/json; charset=utf-8", json, jlen);
    else
        send_response(fd, "500 Internal Server Error", "text/plain", "out of memory", 13);
    free(json);
}

int serve(int port) {
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "aerodsl: could not start Winsock\n");
        return 2;
    }
#endif

    const char *ui_path = NULL;
    char *probe = load_ui(&ui_path);
    if (!probe) {
        fprintf(stderr,
                "aerodsl: cannot find ui/index.html.\n"
                "Run --serve from the project root, where the ui directory is.\n");
        return 2;
    }
    free(probe);

    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SOCK_BAD) {
        perror("socket");
        return 2;
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind");
        sock_close(fd);
        return 2;
    }
    if (listen(fd, 16) < 0) {
        perror("listen");
        sock_close(fd);
        return 2;
    }

    printf("AeroDSL demonstration UI on http://127.0.0.1:%d\n", port);
    printf("serving the page from %s\n", ui_path);
    printf("press Ctrl-C to stop\n");
    fflush(stdout);

    for (;;) {
        sock_t c = accept(fd, NULL, NULL);
        if (c == SOCK_BAD) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char *body = NULL;
        char *req = read_request(c, &body);

        if (strncmp(req, "POST /compile", 13) == 0) {
            handle_compile(c, body);
        } else if (strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /index.html", 15) == 0) {
            const char *p = NULL;
            char *html = load_ui(&p);
            if (html) {
                send_response(c, "200 OK", "text/html; charset=utf-8", html, strlen(html));
                free(html);
            } else {
                send_response(c, "500 Internal Server Error", "text/plain",
                              "ui/index.html is missing", 24);
            }
        } else {
            send_response(c, "404 Not Found", "text/plain", "not found", 9);
        }

        free(req);
        sock_close(c);
    }
    sock_close(fd);
    return 0;
}
