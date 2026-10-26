/*
 * The symbol table (SPEC section 6).
 *
 * Two lists. `all` owns every symbol ever declared, so the AST can keep
 * pointing at a symbol after its scope closes and so the table can be dumped
 * in full for the demo UI. `stack` holds only the symbols currently in scope,
 * with starts[d] marking where scope depth d begins; closing a scope
 * truncates the stack, which is what makes a name stop resolving.
 *
 * Lookup walks the stack backwards, innermost first, which is what gives
 * shadowing. Because scenarios are small the walk is linear -- there is no
 * hash table to explain away when tracing a lookup by hand.
 *
 * `barrier` makes a procedure body unable to see the main body's variables:
 * below it, only globally declared kinds (aircraft, waypoints, procedures)
 * resolve. A main-body variable lives in main's activation record, so a
 * procedure reading one would be reading another frame.
 */
#include "aerodsl.h"

void symtab_init(SymTab *st) {
    memset(st, 0, sizeof *st);
    st->cap = 8;
    st->starts = xcalloc((size_t)st->cap, sizeof *st->starts);
}

void symtab_free(SymTab *st) {
    for (int i = 0; i < st->all.count; i++) free(st->all.items[i]);
    free(st->all.items);
    free(st->stack.items);
    free(st->starts);
    memset(st, 0, sizeof *st);
}

void symtab_push(SymTab *st) {
    st->depth++;
    if (st->depth + 1 > st->cap) {
        st->cap *= 2;
        st->starts = xrealloc(st->starts, (size_t)st->cap * sizeof *st->starts);
    }
    st->starts[st->depth] = st->stack.count;
}

void symtab_pop(SymTab *st) {
    if (st->depth == 0) return;
    st->stack.count = st->starts[st->depth];
    st->depth--;
}

Sym *symtab_declare(SymTab *st, SymKind kind, Type type, Str name, int line, int col) {
    Sym *s = xcalloc(1, sizeof *s);
    s->kind = kind;
    s->type = type;
    s->name = name;
    s->scope = st->depth;
    s->line = line;
    s->col = col;
    s->id = -1;
    PUSH(st->all, s);
    PUSH(st->stack, s);
    return s;
}

static int is_global_kind(SymKind k) { return k == SYM_FLIGHT || k == SYM_FIX || k == SYM_PROC; }

Sym *symtab_lookup(const SymTab *st, Str name) {
    for (int i = st->stack.count - 1; i >= 0; i--) {
        Sym *s = st->stack.items[i];
        if (i < st->barrier && !is_global_kind(s->kind)) continue;
        if (str_eq(s->name, name)) return s;
    }
    return NULL;
}

Sym *symtab_lookup_local(const SymTab *st, Str name) {
    for (int i = st->stack.count - 1; i >= st->starts[st->depth]; i--) {
        Sym *s = st->stack.items[i];
        if (str_eq(s->name, name)) return s;
    }
    return NULL;
}

/* ---- printing ------------------------------------------------------ */

static const char *const KIND_NAMES[] = {"variable", "parameter", "aircraft", "waypoint",
                                         "procedure"};

const char *sym_kind_name(SymKind k) { return KIND_NAMES[k]; }

void symtab_print(FILE *f, const SymTab *st) {
    char buf[64];
    fprintf(f, "symbol table: %d name%s\n", st->all.count, st->all.count == 1 ? "" : "s");
    fprintf(f, "  %-14s %-10s %-7s %-6s %-5s %-8s %s\n", "name", "kind", "type", "index", "scope",
            "declared", "flags");
    for (int i = 0; i < st->all.count; i++) {
        const Sym *s = st->all.items[i];
        char where[32];
        snprintf(where, sizeof where, "%d:%d", s->line, s->col);
        fprintf(f, "  %-14s %-10s %-7s %-6d %-5d %-8s %s%s\n", str_cstr(s->name, buf, sizeof buf),
                sym_kind_name(s->kind), type_name(s->type), s->id, s->scope, where,
                s->used ? "read " : "", s->assigned ? "assigned" : "");
    }
}
