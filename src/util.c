/* Allocation wrappers, string views, integer lists and bit sets. */
#include "aerodsl.h"

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) abort();
    return p;
}

void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) abort();
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) abort();
    return q;
}

/* Reads a whole stream into a NUL-terminated buffer the caller owns. */
char *read_all(FILE *f) {
    size_t len = 0, cap = 4096;
    char *buf = xmalloc(cap);
    for (;;) {
        size_t n = fread(buf + len, 1, cap - len - 1, f);
        if (n == 0) break;
        len += n;
        if (len + 1 == cap) buf = xrealloc(buf, cap *= 2);
    }
    buf[len] = '\0';
    return buf;
}

int str_is(Str s, const char *lit) {
    size_t n = strlen(lit);
    return (size_t)s.len == n && memcmp(s.ptr, lit, n) == 0;
}

int str_eq(Str a, Str b) {
    return a.len == b.len && memcmp(a.ptr, b.ptr, (size_t)a.len) == 0;
}

char *str_cstr(Str s, char *buf, size_t n) {
    size_t len = (size_t)s.len < n - 1 ? (size_t)s.len : n - 1;
    memcpy(buf, s.ptr, len);
    buf[len] = '\0';
    return buf;
}

/* ---- IntList ---- */

void intlist_push(IntList *l, int v) { PUSH(*l, v); }

int intlist_has(const IntList *l, int v) {
    for (int i = 0; i < l->count; i++)
        if (l->items[i] == v) return 1;
    return 0;
}

void intlist_free(IntList *l) {
    free(l->items);
    l->items = NULL;
    l->count = l->cap = 0;
}

/* ---- BitSet ---- */

#define WBITS (int)(8 * sizeof(unsigned))

void bs_init(BitSet *b, int nbits) {
    b->nbits = nbits;
    b->nwords = (nbits + WBITS - 1) / WBITS;
    if (b->nwords < 1) b->nwords = 1;
    b->w = xcalloc((size_t)b->nwords, sizeof *b->w);
}

void bs_free(BitSet *b) {
    free(b->w);
    b->w = NULL;
    b->nbits = b->nwords = 0;
}

void bs_clear(BitSet *b) { memset(b->w, 0, (size_t)b->nwords * sizeof *b->w); }

void bs_copy(BitSet *dst, const BitSet *src) {
    memcpy(dst->w, src->w, (size_t)src->nwords * sizeof *src->w);
}

int bs_get(const BitSet *b, int i) {
    if (i < 0 || i >= b->nbits) return 0;
    return (b->w[i / WBITS] >> (i % WBITS)) & 1u;
}

void bs_set(BitSet *b, int i) {
    if (i < 0 || i >= b->nbits) return;
    b->w[i / WBITS] |= 1u << (i % WBITS);
}

void bs_unset(BitSet *b, int i) {
    if (i < 0 || i >= b->nbits) return;
    b->w[i / WBITS] &= ~(1u << (i % WBITS));
}

int bs_union(BitSet *dst, const BitSet *src) {
    int changed = 0;
    for (int i = 0; i < dst->nwords; i++) {
        unsigned before = dst->w[i];
        dst->w[i] |= src->w[i];
        if (dst->w[i] != before) changed = 1;
    }
    return changed;
}

int bs_intersect(BitSet *dst, const BitSet *src) {
    int changed = 0;
    for (int i = 0; i < dst->nwords; i++) {
        unsigned before = dst->w[i];
        dst->w[i] &= src->w[i];
        if (dst->w[i] != before) changed = 1;
    }
    return changed;
}

void bs_fill(BitSet *b) {
    memset(b->w, 0xFF, (size_t)b->nwords * sizeof *b->w);
    /* Mask off the bits past nbits, so that two sets built the same way
     * compare equal and bs_count does not over-report. */
    if (b->nbits <= 0) {
        b->w[0] = 0;
        return;
    }
    int extra = b->nwords * WBITS - b->nbits;
    if (extra > 0) b->w[b->nwords - 1] &= ~0u >> extra;
}

int bs_count(const BitSet *b) {
    int n = 0;
    for (int i = 0; i < b->nbits; i++)
        if (bs_get(b, i)) n++;
    return n;
}

int bs_eq(const BitSet *a, const BitSet *b) {
    return memcmp(a->w, b->w, (size_t)a->nwords * sizeof *a->w) == 0;
}
