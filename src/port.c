/*
 * Portability shims, so the same sources build on Linux, macOS and Windows.
 *
 *   memout_open / memout_close
 *       An output FILE* whose contents end up in a heap buffer. POSIX has
 *       open_memstream for exactly this; Windows does not, so there the
 *       stream is a private temporary file that is read back and deleted.
 *
 *   str_find_ci
 *       Case-insensitive strstr. strcasestr is a GNU extension, and the
 *       server only needs it to find "Content-Length:" in request headers.
 *
 * Nothing in the compiler's phases depends on this file: only the JSON
 * phase dump (--json) and the demonstration server use it.
 */
#if !defined(_WIN32) && !defined(AERODSL_NO_MEMSTREAM)
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>

#ifdef _WIN32
/* winnt.h declares an enumerator called TokenType, as does aerodsl.h (the
 * lexer's token kinds); rename the Windows one while its headers are read. */
#define WIN32_LEAN_AND_MEAN
#define TokenType Win32_TokenType
#include <windows.h>
#undef TokenType
#endif

#include "aerodsl.h"

FILE *memout_open(MemOut *m) {
    memset(m, 0, sizeof *m);
#if defined(_WIN32)
    char dir[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, dir);
    if (n == 0 || n > MAX_PATH) strcpy(dir, ".");
    if (GetTempFileNameA(dir, "aer", 0, m->path) == 0) return NULL;
    m->f = fopen(m->path, "w+b");
    if (!m->f) remove(m->path);
#elif defined(AERODSL_NO_MEMSTREAM)
    /* Portable fallback, also used to test the read-back path on Linux. */
    m->f = tmpfile();
#else
    m->f = open_memstream(&m->buf, &m->len);
#endif
    return m->f;
}

char *memout_close(MemOut *m, size_t *len_out) {
    if (!m->f) return NULL;
#if defined(_WIN32) || defined(AERODSL_NO_MEMSTREAM)
    char *buf = NULL;
    long n = -1;
    if (fflush(m->f) == 0 && fseek(m->f, 0, SEEK_END) == 0) n = ftell(m->f);
    if (n >= 0 && fseek(m->f, 0, SEEK_SET) == 0) {
        buf = xmalloc((size_t)n + 1);
        size_t got = fread(buf, 1, (size_t)n, m->f);
        buf[got] = '\0';
        m->len = got;
    }
    fclose(m->f);
#if defined(_WIN32)
    remove(m->path);
#endif
    m->f = NULL;
    m->buf = buf;
#else
    fclose(m->f);
    m->f = NULL;
#endif
    if (len_out) *len_out = m->len;
    return m->buf;
}

const char *str_find_ci(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    if (n == 0) return hay;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] &&
               tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == n) return hay;
    }
    return NULL;
}
