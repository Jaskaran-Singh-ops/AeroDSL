/*
 * AeroDSL recursive-descent parser (SPEC section 4).
 *
 * One function per grammar production, so the parser can be read next to the
 * grammar and the correspondence checked by eye. Expressions use one function
 * per precedence level, which makes the precedence structural rather than
 * table-driven.
 *
 * Names are resolved during the parse, because the grammar is
 * declare-before-use: by the time a name is read, its declaration has been
 * seen. Types are not checked here -- that is sema's job -- so every Expr
 * leaves the parser with type TY_ERROR and a resolved Sym where applicable.
 *
 * On an error the parser reports it, then skips forward to a token that can
 * begin the next statement or declaration and carries on, so one mistake does
 * not hide the rest of the file.
 */
#include "aerodsl.h"

typedef struct {
    const Token *t;
    int n, pos;
    DiagList *d;
    SymTab *st;
    Program *p;
    int *nvregs;   /* virtual-register counter of the function being parsed */
    int in_proc;
} P;

/* ---- token plumbing ----------------------------------------------- */

static const Token *peek(P *p) { return &p->t[p->pos]; }
static TokenType at(P *p) { return p->t[p->pos].type; }

static const Token *advance(P *p) {
    const Token *t = &p->t[p->pos];
    if (t->type != T_EOF) p->pos++;
    return t;
}

static int match(P *p, TokenType ty) {
    if (at(p) != ty) return 0;
    p->pos++;
    return 1;
}

static void err_at(P *p, const Token *t, const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_add(p->d, PH_SYNTAX, SEV_ERROR, t->line, t->col, "%s", msg);
}

static void sem_at(P *p, const Token *t, const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_add(p->d, PH_SEMANTIC, SEV_ERROR, t->line, t->col, "%s", msg);
}

/* Describes the current token for a diagnostic. */
static const char *here(P *p, char *buf, size_t n) {
    const Token *t = peek(p);
    if (t->type == T_EOF) return "end of input";
    snprintf(buf, n, "'%.*s'", t->text.len, t->text.ptr);
    return buf;
}

/* Consumes a token of the given type, or reports what was found instead.
 * The token is not consumed on failure, so the caller's resynchronisation
 * can see it. */
static const Token *expect(P *p, TokenType ty, const char *what) {
    if (at(p) == ty) return advance(p);
    char buf[64];
    err_at(p, peek(p), "expected %s, found %s", what, here(p, buf, sizeof buf));
    return NULL;
}

/* ---- resynchronisation -------------------------------------------- */

static int starts_stmt(TokenType t) {
    switch (t) {
    case T_LET:
    case T_CLEAR:
    case T_ADVANCE:
    case T_IF:
    case T_WHILE:
    case T_ASSERT:
    case T_CALL:
    case T_PRINT:
    case T_RETURN:
    case T_IDENT:
    /* A callsign cannot legally begin a statement, but accepting it here
     * turns `AI202 = 4` into "cannot assign to an aircraft" instead of a
     * bare "expected a statement". */
    case T_CALLSIGN: return 1;
    default: return 0;
    }
}

static int starts_item(TokenType t) {
    return starts_stmt(t) || t == T_FIX || t == T_FLIGHT || t == T_PROC;
}

static int closes_block(TokenType t) { return t == T_END || t == T_ELSE || t == T_EOF; }

/* Can this token begin a value? Used to report a missing operand at the
 * keyword that needs it, rather than at whatever follows. */
static int starts_value(TokenType t) {
    switch (t) {
    case T_NUMBER:
    case T_FLEVEL:
    case T_SPEED:
    case T_IDENT:
    case T_CALLSIGN:
    case T_TRUE:
    case T_FALSE:
    case T_LPAREN:
    case T_MINUS:
    case T_NOT: return 1;
    default: return 0;
    }
}

/* AeroDSL has no statement terminator, but statements are written one per
 * line, so the first token of a line is the most reliable place to pick the
 * parse back up: it stops one bad statement from being re-parsed in pieces.
 * A block terminator stops the skip wherever it appears, since `end` often
 * shares a line with the statement before it. */
static int first_on_line(const P *p, int pos) {
    return pos == 0 || p->t[pos - 1].line != p->t[pos].line;
}

static void sync_to(P *p, int (*starts)(TokenType)) {
    /* No unconditional skip here. A failed statement has usually already
     * consumed its own tokens, leaving the parser exactly where the next
     * statement begins; skipping one more would swallow that statement
     * whole. Forward progress is guaranteed by the caller instead, which
     * advances only if the position did not move at all. */
    while (at(p) != T_EOF && !closes_block(at(p))) {
        if (starts(at(p)) && first_on_line(p, p->pos)) return;
        advance(p);
    }
}

static void sync_stmt(P *p) { sync_to(p, starts_stmt); }
static void sync_item(P *p) { sync_to(p, starts_item); }

/* ---- declarations of names ---------------------------------------- */

static int is_builtin_name(Str s) {
    for (int i = 0; i < BI_COUNT; i++)
        if (str_is(s, BUILTINS[i].name)) return 1;
    return 0;
}

/* Declares a variable or parameter, reporting a clash in the same scope and
 * a clash with a built-in function name. Always returns a usable symbol. */
static Sym *declare_local(P *p, SymKind kind, Type ty, const Token *name) {
    if (is_builtin_name(name->text))
        sem_at(p, name, "'%.*s' is the name of a built-in function and cannot be redeclared",
               name->text.len, name->text.ptr);

    Sym *prev = symtab_lookup_local(p->st, name->text);
    if (prev)
        sem_at(p, name, "'%.*s' is already declared in this scope, at line %d column %d",
               name->text.len, name->text.ptr, prev->line, prev->col);

    Sym *s = symtab_declare(p->st, kind, ty, name->text, name->line, name->col);
    s->id = (*p->nvregs)++;
    return s;
}

/* ---- expressions (SPEC 4.3) ---------------------------------------- */

static Expr *parse_expr(P *p);

static int field_of(TokenType t, Field *out) {
    switch (t) {
    case T_ALT: *out = FLD_ALT; return 1;
    case T_HDG: *out = FLD_HDG; return 1;
    case T_SPD: *out = FLD_SPD; return 1;
    case T_LAT: *out = FLD_LAT; return 1;
    case T_LON: *out = FLD_LON; return 1;
    default: return 0;
    }
}

static int builtin_of(Str s, Builtin *out) {
    for (int i = 0; i < BI_COUNT; i++)
        if (str_is(s, BUILTINS[i].name)) {
            *out = (Builtin)i;
            return 1;
        }
    return 0;
}

/* Looks a name up and reports it if undeclared, with a hint when the name
 * looks like a mistyped flight level (SPEC 3.4). */
static Sym *resolve(P *p, const Token *t) {
    Sym *s = symtab_lookup(p->st, t->text);
    if (s) {
        s->used = 1;
        return s;
    }
    Str n = t->text;
    if (n.len >= 2 && n.ptr[0] == 'F' && n.ptr[1] == 'L')
        sem_at(p, t, "'%.*s' is not declared (a flight level is written FL and exactly three "
                     "digits, as in FL320)",
               n.len, n.ptr);
    else if (p->in_proc && n.len > 0 && n.ptr[0] >= 'a' && n.ptr[0] <= 'z')
        sem_at(p, t, "'%.*s' is not declared; a procedure can only use its own parameters and "
                     "globally declared aircraft, waypoints and procedures",
               n.len, n.ptr);
    else
        sem_at(p, t, "'%.*s' is not declared", n.len, n.ptr);
    return NULL;
}

static Expr *parse_builtin_call(P *p, const Token *name, Builtin fn) {
    Expr *e = expr_new(EX_BUILTIN, name->line, name->col);
    e->as.call.fn = fn;
    if (!expect(p, T_LPAREN, "'(' after a built-in function name")) return e;

    int count = 0;
    if (at(p) != T_RPAREN) {
        do {
            Expr *arg = parse_expr(p);
            if (count < 2) {
                e->as.call.args[count] = arg;
            } else {
                err_at(p, name, "%s takes at most 2 arguments", BUILTINS[fn].name);
                expr_free(arg);
            }
            count++;
        } while (match(p, T_COMMA));
    }
    e->as.call.nargs = count < 2 ? count : 2;
    /* Record the real count so sema reports the arity, not the clamped one. */
    if (count != BUILTINS[fn].arity)
        sem_at(p, name, "%s takes %d argument%s, but %d %s given", BUILTINS[fn].name,
               BUILTINS[fn].arity, BUILTINS[fn].arity == 1 ? "" : "s", count,
               count == 1 ? "was" : "were");
    expect(p, T_RPAREN, "')' to close the argument list");
    return e;
}

static Expr *parse_primary(P *p) {
    const Token *t = peek(p);
    switch (t->type) {
    case T_NUMBER:
    case T_FLEVEL:
    case T_SPEED: {
        advance(p);
        Expr *e = expr_new(EX_NUM, t->line, t->col);
        e->as.num = t->num;
        return e;
    }
    case T_TRUE:
    case T_FALSE: {
        advance(p);
        Expr *e = expr_new(EX_BOOL, t->line, t->col);
        e->as.bval = t->type == T_TRUE;
        return e;
    }
    case T_IDENT: {
        advance(p);
        Builtin fn;
        if (builtin_of(t->text, &fn)) {
            if (at(p) != T_LPAREN) {
                err_at(p, t, "%s is a built-in function and needs an argument list",
                       BUILTINS[fn].name);
                return expr_new(EX_NUM, t->line, t->col);
            }
            return parse_builtin_call(p, t, fn);
        }
        Expr *e = expr_new(EX_SYM, t->line, t->col);
        e->as.sym = resolve(p, t);
        return e;
    }
    case T_CALLSIGN: {
        advance(p);
        Expr *e = expr_new(EX_SYM, t->line, t->col);
        e->as.sym = resolve(p, t);
        return e;
    }
    case T_LPAREN: {
        advance(p);
        Expr *e = parse_expr(p);
        expect(p, T_RPAREN, "')' to close a parenthesised expression");
        return e;
    }
    default: {
        char buf[64];
        err_at(p, t, "expected a value, found %s", here(p, buf, sizeof buf));
        /* Consume the offending token so the caller makes progress. */
        if (t->type != T_EOF && !closes_block(t->type) && !starts_stmt(t->type)) advance(p);
        return expr_new(EX_NUM, t->line, t->col);
    }
    }
}

static Expr *parse_postfix(P *p) {
    Expr *e = parse_primary(p);
    while (at(p) == T_DOT) {
        const Token *dot = advance(p);
        Field f;
        if (!field_of(at(p), &f)) {
            char buf[64];
            err_at(p, peek(p), "expected a flight state field (alt, hdg, spd, lat or lon), found %s",
                   here(p, buf, sizeof buf));
            return e;
        }
        advance(p);
        Expr *fe = expr_new(EX_FIELD, dot->line, dot->col);
        fe->as.fld.obj = e;
        fe->as.fld.field = f;
        e = fe;
    }
    return e;
}

static Expr *parse_unary(P *p) {
    if (at(p) == T_MINUS) {
        const Token *t = advance(p);
        Expr *e = expr_new(EX_UN, t->line, t->col);
        e->as.un.op = OP_NEG;
        e->as.un.e = parse_unary(p);
        return e;
    }
    return parse_postfix(p);
}

static Expr *binary(BinOp op, const Token *t, Expr *l, Expr *r) {
    Expr *e = expr_new(EX_BIN, t->line, t->col);
    e->as.bin.op = op;
    e->as.bin.l = l;
    e->as.bin.r = r;
    return e;
}

static Expr *parse_mul(P *p) {
    Expr *e = parse_unary(p);
    for (;;) {
        BinOp op;
        if (at(p) == T_STAR)
            op = OP_MUL;
        else if (at(p) == T_SLASH)
            op = OP_DIV;
        else if (at(p) == T_PERCENT)
            op = OP_MOD;
        else
            return e;
        const Token *t = advance(p);
        e = binary(op, t, e, parse_unary(p));
    }
}

static Expr *parse_add(P *p) {
    Expr *e = parse_mul(p);
    for (;;) {
        BinOp op;
        if (at(p) == T_PLUS)
            op = OP_ADD;
        else if (at(p) == T_MINUS)
            op = OP_SUB;
        else
            return e;
        const Token *t = advance(p);
        e = binary(op, t, e, parse_mul(p));
    }
}

/* At most one comparison: `a < b < c` is rejected (SPEC 4.3). */
static Expr *parse_rel(P *p) {
    Expr *e = parse_add(p);
    BinOp op;
    switch (at(p)) {
    case T_LT: op = OP_LT; break;
    case T_LE: op = OP_LE; break;
    case T_GT: op = OP_GT; break;
    case T_GE: op = OP_GE; break;
    case T_EQEQ: op = OP_EQ; break;
    case T_NE: op = OP_NE; break;
    default: return e;
    }
    const Token *t = advance(p);
    e = binary(op, t, e, parse_add(p));

    switch (at(p)) {
    case T_LT:
    case T_LE:
    case T_GT:
    case T_GE:
    case T_EQEQ:
    case T_NE:
        err_at(p, peek(p),
               "comparisons do not chain in AeroDSL; write 'a < b and b < c' instead of 'a < b < c'");
        advance(p);
        expr_free(parse_add(p)); /* parsed so the rest of the line is consumed */
        break;
    default: break;
    }
    return e;
}

static Expr *parse_not(P *p) {
    if (at(p) == T_NOT) {
        const Token *t = advance(p);
        Expr *e = expr_new(EX_UN, t->line, t->col);
        e->as.un.op = OP_NOT;
        e->as.un.e = parse_not(p);
        return e;
    }
    return parse_rel(p);
}

static Expr *parse_and(P *p) {
    Expr *e = parse_not(p);
    while (at(p) == T_AND) {
        const Token *t = advance(p);
        e = binary(OP_AND, t, e, parse_not(p));
    }
    return e;
}

static Expr *parse_expr(P *p) {
    Expr *e = parse_and(p);
    while (at(p) == T_OR) {
        const Token *t = advance(p);
        e = binary(OP_OR, t, e, parse_and(p));
    }
    return e;
}

/*
 * Parses an operand, or reports it as missing at the keyword that needs it.
 * Without this, `clear AI202 climb` followed by another statement reports
 * "expected a value" at the *next* line, which is where the parser noticed
 * rather than where the mistake is. A NULL return is tolerated by every
 * later phase, which treats it as an already-reported error.
 */
static Expr *parse_operand(P *p, const Token *owner, const char *needs) {
    if (starts_value(at(p))) return parse_expr(p);
    char buf[64];
    err_at(p, owner, "'%.*s' requires %s, found %s", owner->text.len, owner->text.ptr, needs,
           here(p, buf, sizeof buf));
    return NULL;
}

/* ---- statements (SPEC 4.2) ---------------------------------------- */

static void parse_block(P *p, StmtList *out);

static Stmt *parse_let(P *p) {
    const Token *kw = advance(p); /* let */
    const Token *name = expect(p, T_IDENT, "a variable name in lower case");
    if (!name) return NULL;
    const Token *eq = expect(p, T_ASSIGN, "'=' after the variable name");
    if (!eq) return NULL;
    Expr *init = parse_operand(p, eq, "a value");

    /* Declared after the initialiser is parsed, so `let x = x` reports x as
     * undeclared rather than silently reading itself. */
    Sym *s = declare_local(p, SYM_VAR, TY_ERROR, name);
    s->assigned = 1;

    Stmt *st = stmt_new(ST_LET, kw->line, kw->col);
    st->as.let.sym = s;
    st->as.let.init = init;
    return st;
}

static Stmt *parse_assign(P *p) {
    const Token *name = advance(p); /* IDENT */
    Sym *s = symtab_lookup(p->st, name->text);
    if (!s)
        s = resolve(p, name); /* reports the error */
    const Token *eq = expect(p, T_ASSIGN, "'=' in an assignment");
    if (!eq) return NULL;
    Expr *init = parse_operand(p, eq, "a value");

    if (s && s->kind != SYM_VAR && s->kind != SYM_PARAM) {
        static const char *const what[] = {"a variable", "a parameter", "an aircraft",
                                           "a waypoint", "a procedure"};
        sem_at(p, name, "cannot assign to '%.*s', which is %s", name->text.len, name->text.ptr,
               what[s->kind]);
        s = NULL;
    }
    if (!s) {
        expr_free(init);
        return NULL;
    }
    s->assigned = 1;

    Stmt *st = stmt_new(ST_ASSIGN, name->line, name->col);
    st->as.let.sym = s;
    st->as.let.init = init;
    return st;
}

static Stmt *parse_clear(P *p) {
    const Token *kw = advance(p); /* clear */
    Expr *flight = parse_operand(p, kw, "an aircraft");
    if (!flight) return NULL;

    ClearKind kind;
    switch (at(p)) {
    case T_CLIMB: kind = CL_CLIMB; break;
    case T_DESCEND: kind = CL_DESCEND; break;
    case T_HEADING: kind = CL_HEADING; break;
    case T_SPEED_KW: kind = CL_SPEED; break;
    case T_DIRECT: kind = CL_DIRECT; break;
    default: {
        char buf[64];
        err_at(p, peek(p),
               "expected a clearance (climb, descend, heading, speed or direct), found %s",
               here(p, buf, sizeof buf));
        expr_free(flight);
        return NULL;
    }
    }
    const Token *opkw = advance(p);
    const ClearInfo *ci = &CLEARS[kind];
    char needs[96];
    if (ci->operand == TY_FIX)
        snprintf(needs, sizeof needs, "a waypoint");
    else
        snprintf(needs, sizeof needs, "a value in %s", ci->unit);
    Expr *operand = parse_operand(p, opkw, needs);

    Stmt *st = stmt_new(ST_CLEAR, kw->line, kw->col);
    st->as.clear.flight = flight;
    st->as.clear.kind = kind;
    st->as.clear.operand = operand;
    return st;
}

static Stmt *parse_advance(P *p) {
    const Token *kw = advance(p);
    Stmt *st = stmt_new(ST_ADVANCE, kw->line, kw->col);
    st->as.advance.secs = parse_operand(p, kw, "a number of seconds");
    return st;
}

static Stmt *parse_if(P *p) {
    const Token *kw = advance(p);
    Expr *cond = parse_operand(p, kw, "a condition");
    expect(p, T_THEN, "'then' after the condition");

    Stmt *st = stmt_new(ST_IF, kw->line, kw->col);
    st->as.ifs.cond = cond;

    symtab_push(p->st);
    parse_block(p, &st->as.ifs.then_body);
    symtab_pop(p->st);

    if (match(p, T_ELSE)) {
        st->as.ifs.has_else = 1;
        symtab_push(p->st);
        parse_block(p, &st->as.ifs.else_body);
        symtab_pop(p->st);
    }
    expect(p, T_END, "'end' to close the if");
    return st;
}

static Stmt *parse_while(P *p) {
    const Token *kw = advance(p);
    Expr *cond = parse_operand(p, kw, "a condition");
    expect(p, T_DO, "'do' after the condition");

    Stmt *st = stmt_new(ST_WHILE, kw->line, kw->col);
    st->as.whiles.cond = cond;

    symtab_push(p->st);
    parse_block(p, &st->as.whiles.body);
    symtab_pop(p->st);

    expect(p, T_END, "'end' to close the while");
    return st;
}

static Stmt *parse_assert(P *p) {
    const Token *kw = advance(p);
    Stmt *st = stmt_new(ST_ASSERT, kw->line, kw->col);
    st->as.assertion.cond = parse_operand(p, kw, "a condition");
    st->as.assertion.msg = -1;
    if (match(p, T_COMMA)) {
        const Token *s = expect(p, T_STRING, "a message in double quotes");
        if (s) st->as.assertion.msg = program_add_string(p->p, s->text);
    }
    return st;
}

static Stmt *parse_call(P *p) {
    const Token *kw = advance(p); /* call */
    const Token *name = expect(p, T_IDENT, "a procedure name");
    if (!name) return NULL;

    Sym *s = symtab_lookup(p->st, name->text);
    if (!s) {
        resolve(p, name);
    } else if (s->kind != SYM_PROC) {
        sem_at(p, name, "'%.*s' is not a procedure", name->text.len, name->text.ptr);
        s = NULL;
    } else {
        s->used = 1;
    }

    Stmt *st = stmt_new(ST_CALL, kw->line, kw->col);
    st->as.call.proc = s;

    if (!expect(p, T_LPAREN, "'(' after the procedure name")) return st;
    int count = 0;
    if (at(p) != T_RPAREN) {
        do {
            Expr *a = parse_expr(p);
            if (count < 8) {
                st->as.call.args[count] = a;
            } else {
                err_at(p, name, "a procedure call takes at most 8 arguments");
                expr_free(a);
            }
            count++;
        } while (match(p, T_COMMA));
    }
    st->as.call.nargs = count < 8 ? count : 8;
    expect(p, T_RPAREN, "')' to close the argument list");
    /* The statement is kept even when the callee did not resolve: the
     * argument expressions belong to it, and sema still checks them. */
    return st;
}

static Stmt *parse_print(P *p) {
    const Token *kw = advance(p);
    Stmt *st = stmt_new(ST_PRINT, kw->line, kw->col);
    int count = 0;
    do {
        if (count >= 8) {
            err_at(p, kw, "print takes at most 8 items");
            if (at(p) == T_STRING)
                advance(p);
            else
                expr_free(parse_expr(p));
            continue;
        }
        if (at(p) == T_STRING) {
            const Token *s = advance(p);
            st->as.print.msgs[count] = program_add_string(p->p, s->text);
            st->as.print.args[count] = NULL;
        } else {
            st->as.print.msgs[count] = -1;
            st->as.print.args[count] = parse_expr(p);
        }
        count++;
    } while (match(p, T_COMMA));
    st->as.print.nargs = count < 8 ? count : 8;
    return st;
}

static Stmt *parse_stmt(P *p) {
    switch (at(p)) {
    case T_LET: return parse_let(p);
    case T_IDENT:
    case T_CALLSIGN: return parse_assign(p);
    case T_CLEAR: return parse_clear(p);
    case T_ADVANCE: return parse_advance(p);
    case T_IF: return parse_if(p);
    case T_WHILE: return parse_while(p);
    case T_ASSERT: return parse_assert(p);
    case T_CALL: return parse_call(p);
    case T_PRINT: return parse_print(p);
    case T_RETURN: {
        const Token *kw = advance(p);
        return stmt_new(ST_RETURN, kw->line, kw->col);
    }
    default: return NULL;
    }
}

/*
 * Parses statements until a block terminator.
 *
 * Resynchronisation runs only after a statement that reported something.
 * Skipping tokens after a *successful* statement would silently swallow a
 * stray token such as the `2` in `advance 1 2`; left in place, the next
 * iteration reports it.
 */
static void parse_block(P *p, StmtList *out) {
    while (!closes_block(at(p))) {
        int before = p->pos, errs = p->d->count;
        if (starts_stmt(at(p))) {
            Stmt *s = parse_stmt(p);
            if (s) stmtlist_push(out, s);
        } else {
            char buf[64];
            err_at(p, peek(p), "expected a statement, found %s", here(p, buf, sizeof buf));
        }
        if (p->d->count != errs) sync_stmt(p);
        if (p->pos == before) advance(p); /* never spin */
    }
}

/* ---- declarations (SPEC 4.1) ---------------------------------------- */

/* A latitude or longitude: an optionally negated number literal. */
static int parse_coord(P *p, double *out, const char *what) {
    int neg = match(p, T_MINUS);
    const Token *t = expect(p, T_NUMBER, what);
    if (!t) return 0;
    *out = neg ? -t->num : t->num;
    return 1;
}

static void parse_fix(P *p) {
    advance(p); /* fix */
    const Token *name = expect(p, T_CALLSIGN, "a waypoint name in UPPER CASE");
    if (!name) return;

    Sym *prev = symtab_lookup(p->st, name->text);
    if (prev)
        sem_at(p, name, "'%.*s' is already declared, at line %d column %d", name->text.len,
               name->text.ptr, prev->line, prev->col);

    double lat = 0, lon = 0;
    if (!parse_coord(p, &lat, "a latitude in degrees")) return;
    if (!parse_coord(p, &lon, "a longitude in degrees")) return;

    PUSHN(p->p->fixes, p->p->nfixes, p->p->fixes_cap,
          (FixDecl){name->text, lat, lon, name->line, name->col});
    if (!prev) {
        Sym *s = symtab_declare(p->st, SYM_FIX, TY_FIX, name->text, name->line, name->col);
        s->id = p->p->nfixes - 1;
    }
}

/* A `flight` declaration is also a statement: the aircraft enters the
 * airspace where the declaration appears, which is what lets a scenario
 * introduce traffic part-way through (SPEC 4.1). */
static Stmt *parse_flight(P *p) {
    const Token *kw = advance(p); /* flight */
    const Token *name = expect(p, T_CALLSIGN, "an aircraft callsign in UPPER CASE");
    if (!name) return NULL;

    Sym *prev = symtab_lookup(p->st, name->text);
    if (prev)
        sem_at(p, name, "'%.*s' is already declared, at line %d column %d", name->text.len,
               name->text.ptr, prev->line, prev->col);

    /* An optional aircraft type, which the language does not otherwise use
     * but which the generated scenario file needs. */
    Str type = {NULL, 0};
    if (match(p, T_TYPE)) {
        const Token *ty = expect(p, T_CALLSIGN, "an aircraft type in UPPER CASE, such as A320");
        if (ty) type = ty->text;
    }

    if (!expect(p, T_ENTER, "'enter' followed by the waypoint the aircraft enters at"))
        return NULL;
    const Token *fix = expect(p, T_CALLSIGN, "a waypoint name");
    if (!fix) return NULL;

    int fix_index = -1;
    Sym *fs = symtab_lookup(p->st, fix->text);
    if (!fs)
        resolve(p, fix);
    else if (fs->kind != SYM_FIX)
        sem_at(p, fix, "'%.*s' is not a waypoint", fix->text.len, fix->text.ptr);
    else {
        fs->used = 1;
        fix_index = fs->id;
    }

    FlightDecl fl = {name->text, type, fix_index, NULL, NULL, NULL, name->line, name->col};
    const Token *k;
    if ((k = expect(p, T_ALT, "'alt' and the initial altitude")))
        fl.alt = parse_operand(p, k, "an altitude in ft");
    if ((k = expect(p, T_HDG, "'hdg' and the initial heading")))
        fl.hdg = parse_operand(p, k, "a heading in degrees");
    if ((k = expect(p, T_SPD, "'spd' and the initial speed")))
        fl.spd = parse_operand(p, k, "a speed in kt");

    PUSHN(p->p->flights, p->p->nflights, p->p->flights_cap, fl);
    int index = p->p->nflights - 1;
    if (!prev) {
        Sym *s = symtab_declare(p->st, SYM_FLIGHT, TY_FLIGHT, name->text, name->line, name->col);
        s->id = index;
    }

    Stmt *st = stmt_new(ST_ENTER, kw->line, kw->col);
    st->as.enter.flight = index;
    return st;
}

static int parse_type(P *p, Type *out) {
    switch (at(p)) {
    case T_TY_NUM: *out = TY_NUM; break;
    case T_TY_BOOL: *out = TY_BOOL; break;
    case T_FLIGHT: *out = TY_FLIGHT; break;
    case T_FIX: *out = TY_FIX; break;
    default: {
        char buf[64];
        err_at(p, peek(p), "expected a parameter type (num, bool, flight or fix), found %s",
               here(p, buf, sizeof buf));
        return 0;
    }
    }
    advance(p);
    return 1;
}

static void parse_proc(P *p) {
    advance(p); /* proc */
    const Token *name = expect(p, T_IDENT, "a procedure name in lower case");
    if (!name) {
        sync_item(p);
        return;
    }

    if (is_builtin_name(name->text))
        sem_at(p, name, "'%.*s' is the name of a built-in function and cannot be redeclared",
               name->text.len, name->text.ptr);
    Sym *prev = symtab_lookup(p->st, name->text);
    if (prev)
        sem_at(p, name, "'%.*s' is already declared, at line %d column %d", name->text.len,
               name->text.ptr, prev->line, prev->col);

    ProcDecl *pr = xcalloc(1, sizeof *pr);
    pr->name = name->text;
    pr->line = name->line;
    pr->col = name->col;

    Sym *psym = NULL;
    if (!prev) {
        psym = symtab_declare(p->st, SYM_PROC, TY_VOID, name->text, name->line, name->col);
        psym->id = p->p->nprocs;
        psym->proc = pr;
        pr->sym = psym;
    }
    PUSHN(p->p->procs, p->p->nprocs, p->p->procs_cap, pr);

    /* The body sees its parameters and the globals, and nothing of main. */
    int saved_barrier = p->st->barrier;
    int *saved_vregs = p->nvregs;
    int saved_in_proc = p->in_proc;
    p->st->barrier = p->st->stack.count;
    p->nvregs = &pr->nvregs;
    p->in_proc = 1;
    symtab_push(p->st);

    if (expect(p, T_LPAREN, "'(' after the procedure name")) {
        if (at(p) != T_RPAREN) {
            do {
                Type ty;
                if (!parse_type(p, &ty)) break;
                const Token *pname = expect(p, T_IDENT, "a parameter name in lower case");
                if (!pname) break;
                Sym *s = declare_local(p, SYM_PARAM, ty, pname);
                s->assigned = 1;
                if (pr->nparams < 8)
                    pr->params[pr->nparams++] = s;
                else
                    err_at(p, pname, "a procedure takes at most 8 parameters");
            } while (match(p, T_COMMA));
        }
        expect(p, T_RPAREN, "')' to close the parameter list");
    }

    parse_block(p, &pr->body);
    expect(p, T_END, "'end' to close the procedure");

    symtab_pop(p->st);
    p->st->barrier = saved_barrier;
    p->nvregs = saved_vregs;
    p->in_proc = saved_in_proc;
}

/* ---- entry point --------------------------------------------------- */

void parse(const TokenList *tokens, Program *out, SymTab *st, DiagList *diags) {
    P parser = {tokens->items, tokens->count, 0, diags, st, out, &out->nvregs, 0};
    P *p = &parser;

    if (!expect(p, T_SCENARIO, "a scenario declaration, as in: scenario \"name\"")) {
        sync_item(p);
    } else {
        const Token *title = expect(p, T_STRING, "the scenario name in double quotes");
        if (title) out->scenario = title->text;
    }

    while (at(p) != T_EOF) {
        int before = p->pos, errs = p->d->count;
        switch (at(p)) {
        case T_FIX: parse_fix(p); break;
        case T_FLIGHT: {
            Stmt *s = parse_flight(p);
            if (s) stmtlist_push(&out->main_body, s);
            break;
        }
        case T_PROC: parse_proc(p); break;
        default:
            if (starts_stmt(at(p))) {
                Stmt *s = parse_stmt(p);
                if (s) stmtlist_push(&out->main_body, s);
            } else {
                char buf[64];
                if (at(p) == T_END)
                    err_at(p, peek(p), "'end' here does not close anything");
                else if (at(p) == T_ELSE)
                    err_at(p, peek(p), "'else' here is not inside an if");
                else
                    err_at(p, peek(p), "expected a declaration or a statement, found %s",
                           here(p, buf, sizeof buf));
            }
            break;
        }
        if (p->d->count != errs) sync_item(p);
        if (p->pos == before) advance(p); /* never spin */
    }
}
