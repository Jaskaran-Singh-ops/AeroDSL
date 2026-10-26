/*
 * AeroDSL semantic analyser (SPEC sections 5 to 7).
 *
 * Walks the AST once, annotating every expression with a type and reporting
 * the name, type and arity errors of SPEC 7.1, plus the code-quality
 * warnings of SPEC 7.5.
 *
 * Operand *range* checking is deliberately not here: it runs in
 * src/analysis.c on the optimised three-address code, after constant
 * propagation, so that `let h = 450  clear F heading h` is caught at compile
 * time and not just a literal 450. Keeping it in one place also keeps it
 * from being reported twice.
 *
 * Error suppression: a subexpression that already failed has type TY_ERROR,
 * and an operator with a TY_ERROR operand reports nothing and yields
 * TY_ERROR in turn. One mistake therefore produces one message.
 */
#include "aerodsl.h"

typedef struct {
    Program *p;
    SymTab *st;
    DiagList *d;
    ProcDecl *cur;      /* the procedure being checked, NULL in main */
    int saw_advance;
} S;

static void err(S *s, int line, int col, const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_add(s->d, PH_SEMANTIC, SEV_ERROR, line, col, "%s", msg);
}

static void warn(S *s, int line, int col, const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_add(s->d, PH_SEMANTIC, SEV_WARNING, line, col, "%s", msg);
}

/* ---- constant evaluation ------------------------------------------ *
 * Only literal arithmetic. Anything involving a variable is left to the
 * constant propagation in the optimiser.
 */
static int const_eval(const Expr *e, double *out) {
    if (!e) return 0;
    double a, b;
    switch (e->kind) {
    case EX_NUM: *out = e->as.num; return 1;
    case EX_UN:
        if (e->as.un.op != OP_NEG || !const_eval(e->as.un.e, &a)) return 0;
        *out = -a;
        return 1;
    case EX_BIN:
        if (!const_eval(e->as.bin.l, &a) || !const_eval(e->as.bin.r, &b)) return 0;
        switch (e->as.bin.op) {
        case OP_ADD: *out = a + b; return 1;
        case OP_SUB: *out = a - b; return 1;
        case OP_MUL: *out = a * b; return 1;
        case OP_DIV:
            if (b == 0) return 0;
            *out = a / b;
            return 1;
        case OP_MOD:
            if (b == 0) return 0;
            *out = a - b * (double)(long long)(a / b);
            return 1;
        default: return 0;
        }
    default: return 0;
    }
}

/* ---- expressions --------------------------------------------------- */

static Type check_expr(S *s, Expr *e);

/* Reports a type mismatch unless the operand already failed. */
static Type want(S *s, Expr *e, Type got, Type expected, const char *role) {
    if (got == TY_ERROR || got == expected) return got;
    err(s, e->line, e->col, "%s must be %s, found %s", role, type_name(expected), type_name(got));
    return TY_ERROR;
}

static Type check_binary(S *s, Expr *e) {
    Type l = check_expr(s, e->as.bin.l);
    Type r = check_expr(s, e->as.bin.r);
    BinOp op = e->as.bin.op;

    switch (op) {
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD: {
        want(s, e->as.bin.l, l, TY_NUM, "the left operand of an arithmetic operator");
        want(s, e->as.bin.r, r, TY_NUM, "the right operand of an arithmetic operator");
        if (l != TY_NUM || r != TY_NUM) return TY_ERROR;
        double k;
        if ((op == OP_DIV || op == OP_MOD) && const_eval(e->as.bin.r, &k) && k == 0)
            err(s, e->as.bin.r->line, e->as.bin.r->col, "division by zero");
        return TY_NUM;
    }
    case OP_LT:
    case OP_LE:
    case OP_GT:
    case OP_GE:
        want(s, e->as.bin.l, l, TY_NUM, "the left operand of a comparison");
        want(s, e->as.bin.r, r, TY_NUM, "the right operand of a comparison");
        return (l == TY_NUM && r == TY_NUM) ? TY_BOOL : TY_ERROR;
    case OP_EQ:
    case OP_NE:
        if (l == TY_ERROR || r == TY_ERROR) return TY_ERROR;
        if (l != r) {
            err(s, e->line, e->col, "cannot compare %s with %s", type_name(l), type_name(r));
            return TY_ERROR;
        }
        if (l == TY_VOID) {
            err(s, e->line, e->col, "a procedure has no value to compare");
            return TY_ERROR;
        }
        return TY_BOOL;
    case OP_AND:
    case OP_OR:
        want(s, e->as.bin.l, l, TY_BOOL, "the left operand of a logical operator");
        want(s, e->as.bin.r, r, TY_BOOL, "the right operand of a logical operator");
        return (l == TY_BOOL && r == TY_BOOL) ? TY_BOOL : TY_ERROR;
    default: return TY_ERROR;
    }
}

static Type check_expr(S *s, Expr *e) {
    if (!e) return TY_ERROR;
    char buf[64];
    switch (e->kind) {
    case EX_NUM: return e->type = TY_NUM;
    case EX_BOOL: return e->type = TY_BOOL;

    case EX_SYM: {
        Sym *sym = e->as.sym;
        if (!sym) return e->type = TY_ERROR; /* the parser already reported it */
        if (sym->kind == SYM_PROC) {
            err(s, e->line, e->col, "'%s' is a procedure and has no value; use 'call %s(...)'",
                str_cstr(sym->name, buf, sizeof buf), str_cstr(sym->name, buf, sizeof buf));
            return e->type = TY_ERROR;
        }
        if (sym->type == TY_ERROR) return e->type = TY_ERROR;
        return e->type = sym->type;
    }

    case EX_BIN: return e->type = check_binary(s, e);

    case EX_UN: {
        Type t = check_expr(s, e->as.un.e);
        if (e->as.un.op == OP_NEG) {
            want(s, e->as.un.e, t, TY_NUM, "the operand of unary '-'");
            return e->type = (t == TY_NUM) ? TY_NUM : TY_ERROR;
        }
        want(s, e->as.un.e, t, TY_BOOL, "the operand of 'not'");
        return e->type = (t == TY_BOOL) ? TY_BOOL : TY_ERROR;
    }

    case EX_FIELD: {
        Type t = check_expr(s, e->as.fld.obj);
        if (t == TY_ERROR) return e->type = TY_ERROR;
        if (t != TY_FLIGHT) {
            err(s, e->line, e->col, "'.%s' reads the state of an aircraft, but this is %s",
                field_name(e->as.fld.field), type_name(t));
            return e->type = TY_ERROR;
        }
        return e->type = TY_NUM;
    }

    case EX_BUILTIN: {
        const BuiltinInfo *bi = &BUILTINS[e->as.call.fn];
        int ok = 1;
        for (int i = 0; i < e->as.call.nargs; i++) {
            Type t = check_expr(s, e->as.call.args[i]);
            if (i >= bi->arity) continue; /* arity already reported by the parser */
            if (t == TY_ERROR) {
                ok = 0;
                continue;
            }
            if (t != bi->args[i]) {
                err(s, e->as.call.args[i]->line, e->as.call.args[i]->col,
                    "argument %d of %s must be %s, found %s", i + 1, bi->name,
                    type_name(bi->args[i]), type_name(t));
                ok = 0;
            }
        }
        if (e->as.call.nargs != bi->arity) ok = 0;
        return e->type = ok ? bi->ret : TY_ERROR;
    }
    }
    return e->type = TY_ERROR;
}

/* ---- statements ---------------------------------------------------- */

static void check_block(S *s, StmtList *l);

static void check_cond(S *s, Expr *e, const char *what) {
    Type t = check_expr(s, e);
    if (t != TY_ERROR && t != TY_BOOL)
        err(s, e->line, e->col,
            "the condition of %s must be bool, found %s; AeroDSL has no implicit test against "
            "zero, so write a comparison such as '> 0'",
            what, type_name(t));
}

/* True if the statement list contains an `advance`, looking inside nested
 * ifs and whiles but not through a procedure call. */
static int block_has_advance(const StmtList *l, int *saw_call) {
    for (int i = 0; i < l->count; i++) {
        const Stmt *st = l->items[i];
        switch (st->kind) {
        case ST_ADVANCE: return 1;
        case ST_CALL: *saw_call = 1; break;
        case ST_IF:
            if (block_has_advance(&st->as.ifs.then_body, saw_call)) return 1;
            if (block_has_advance(&st->as.ifs.else_body, saw_call)) return 1;
            break;
        case ST_WHILE:
            if (block_has_advance(&st->as.whiles.body, saw_call)) return 1;
            break;
        default: break;
        }
    }
    return 0;
}

/* Collects the variables read by an expression, for the loop-progress check. */
static void expr_reads(const Expr *e, SymList *out) {
    if (!e) return;
    switch (e->kind) {
    case EX_SYM:
        if (e->as.sym && (e->as.sym->kind == SYM_VAR || e->as.sym->kind == SYM_PARAM))
            PUSH(*out, e->as.sym);
        break;
    case EX_BIN:
        expr_reads(e->as.bin.l, out);
        expr_reads(e->as.bin.r, out);
        break;
    case EX_UN: expr_reads(e->as.un.e, out); break;
    case EX_FIELD: expr_reads(e->as.fld.obj, out); break;
    case EX_BUILTIN:
        for (int i = 0; i < e->as.call.nargs; i++) expr_reads(e->as.call.args[i], out);
        break;
    default: break;
    }
}

static int block_assigns_any(const StmtList *l, const SymList *syms) {
    for (int i = 0; i < l->count; i++) {
        const Stmt *st = l->items[i];
        switch (st->kind) {
        case ST_ASSIGN:
            for (int j = 0; j < syms->count; j++)
                if (syms->items[j] == st->as.let.sym) return 1;
            break;
        case ST_IF:
            if (block_assigns_any(&st->as.ifs.then_body, syms)) return 1;
            if (block_assigns_any(&st->as.ifs.else_body, syms)) return 1;
            break;
        case ST_WHILE:
            if (block_assigns_any(&st->as.whiles.body, syms)) return 1;
            break;
        default: break;
        }
    }
    return 0;
}

/*
 * SPEC 7.5 warning 9. A sufficient condition for a loop that cannot make
 * progress: nothing in the body advances the simulation, nothing in the body
 * assigns a variable the condition reads, and the body calls no procedure
 * (which could do either). This is not a decision procedure -- the halting
 * problem forbids one -- so loops it cannot prove anything about are left to
 * the runtime limits instead.
 */
static void check_loop_progress(S *s, const Stmt *st) {
    int saw_call = 0;
    if (block_has_advance(&st->as.whiles.body, &saw_call) || saw_call) return;

    SymList reads = {0};
    expr_reads(st->as.whiles.cond, &reads);
    int progresses = block_assigns_any(&st->as.whiles.body, &reads);
    free(reads.items);
    if (progresses) return;

    warn(s, st->line, st->col,
         "this loop cannot make progress: its body neither advances the simulation nor changes "
         "any variable its condition reads");
}

static void check_clear(S *s, Stmt *st) {
    Type ft = check_expr(s, st->as.clear.flight);
    if (ft != TY_ERROR && ft != TY_FLIGHT)
        err(s, st->as.clear.flight->line, st->as.clear.flight->col,
            "a clearance is issued to an aircraft, but this is %s", type_name(ft));

    const ClearInfo *ci = &CLEARS[st->as.clear.kind];
    Type ot = check_expr(s, st->as.clear.operand);
    if (ot != TY_ERROR && ot != ci->operand) {
        if (ci->operand == TY_NUM)
            err(s, st->as.clear.operand->line, st->as.clear.operand->col,
                "'%s' takes a number in %s, found %s", ci->name, ci->unit, type_name(ot));
        else
            err(s, st->as.clear.operand->line, st->as.clear.operand->col,
                "'%s' takes a waypoint, found %s", ci->name, type_name(ot));
    }

}

static void check_call(S *s, Stmt *st) {
    Sym *sym = st->as.call.proc;
    char buf[64];
    /* Argument expressions are checked whether or not the callee resolved,
     * so that errors inside them are still reported. */
    Type given[8];
    for (int i = 0; i < st->as.call.nargs; i++) given[i] = check_expr(s, st->as.call.args[i]);
    if (!sym || !sym->proc) return;

    ProcDecl *pr = sym->proc;
    if (st->as.call.nargs != pr->nparams) {
        err(s, st->line, st->col, "%s takes %d argument%s, but %d %s given (declared at line %d)",
            str_cstr(pr->name, buf, sizeof buf), pr->nparams, pr->nparams == 1 ? "" : "s",
            st->as.call.nargs, st->as.call.nargs == 1 ? "was" : "were", pr->line);
        return;
    }
    for (int i = 0; i < st->as.call.nargs; i++) {
        if (given[i] == TY_ERROR) continue;
        if (given[i] != pr->params[i]->type)
            err(s, st->as.call.args[i]->line, st->as.call.args[i]->col,
                "argument %d of %s must be %s, found %s", i + 1,
                str_cstr(pr->name, buf, sizeof buf), type_name(pr->params[i]->type),
                type_name(given[i]));
    }
}

static void check_stmt(S *s, Stmt *st) {
    char buf[64];
    switch (st->kind) {
    case ST_LET: {
        Type t = check_expr(s, st->as.let.init);
        if (t == TY_VOID) t = TY_ERROR;
        st->as.let.sym->type = t;
        break;
    }
    case ST_ASSIGN: {
        Type t = check_expr(s, st->as.let.init);
        Sym *sym = st->as.let.sym;
        if (t != TY_ERROR && sym->type != TY_ERROR && t != sym->type)
            err(s, st->line, st->col, "'%s' holds %s, so it cannot be assigned %s",
                str_cstr(sym->name, buf, sizeof buf), type_name(sym->type), type_name(t));
        break;
    }
    case ST_ENTER: {
        FlightDecl *fl = &s->p->flights[st->as.enter.flight];
        struct {
            Expr *e;
            const char *what;
        } parts[] = {{fl->alt, "an initial altitude"},
                     {fl->hdg, "an initial heading"},
                     {fl->spd, "an initial speed"}};
        for (int i = 0; i < 3; i++) {
            if (!parts[i].e) continue;
            Type t = check_expr(s, parts[i].e);
            if (t != TY_ERROR && t != TY_NUM)
                err(s, parts[i].e->line, parts[i].e->col, "%s must be a number, found %s",
                    parts[i].what, type_name(t));
        }
        break;
    }
    case ST_CLEAR: check_clear(s, st); break;
    case ST_ADVANCE: {
        Type t = check_expr(s, st->as.advance.secs);
        if (t != TY_ERROR && t != TY_NUM)
            err(s, st->as.advance.secs->line, st->as.advance.secs->col,
                "'advance' takes a number of seconds, found %s", type_name(t));
        s->saw_advance = 1;
        break;
    }
    case ST_IF:
        check_cond(s, st->as.ifs.cond, "an if");
        check_block(s, &st->as.ifs.then_body);
        if (st->as.ifs.has_else) check_block(s, &st->as.ifs.else_body);
        break;
    case ST_WHILE:
        check_cond(s, st->as.whiles.cond, "a while");
        check_block(s, &st->as.whiles.body);
        check_loop_progress(s, st);
        break;
    case ST_ASSERT: check_cond(s, st->as.assertion.cond, "an assert"); break;
    case ST_CALL: check_call(s, st); break;
    case ST_PRINT:
        for (int i = 0; i < st->as.print.nargs; i++) {
            if (st->as.print.msgs[i] >= 0) continue;
            Type t = check_expr(s, st->as.print.args[i]);
            if (t == TY_VOID)
                err(s, st->as.print.args[i]->line, st->as.print.args[i]->col,
                    "there is no value here to print");
        }
        break;
    case ST_RETURN: break;
    }
}

static void check_block(S *s, StmtList *l) {
    for (int i = 0; i < l->count; i++) {
        check_stmt(s, l->items[i]);
        if (l->items[i]->kind == ST_RETURN && i + 1 < l->count) {
            warn(s, l->items[i + 1]->line, l->items[i + 1]->col,
                 "this statement cannot be reached: the 'return' above always runs first");
            /* Check the rest anyway, so its own errors are still reported. */
        }
    }
}

/* ---- entry point --------------------------------------------------- */

void sema_run(Program *p, SymTab *st, DiagList *diags) {
    S state = {p, st, diags, NULL, 0};
    S *s = &state;
    char buf[64];

    /* Waypoint coordinates are static data and never reach the IR, so they
     * are range-checked here rather than in src/analysis.c. */
    for (int i = 0; i < p->nfixes; i++) {
        const FixDecl *fx = &p->fixes[i];
        if (fx->lat < -90 || fx->lat > 90)
            err(s, fx->line, fx->col, "latitude of %s must be between -90 and 90, found %g",
                str_cstr(fx->name, buf, sizeof buf), fx->lat);
        if (fx->lon < -180 || fx->lon > 180)
            err(s, fx->line, fx->col, "longitude of %s must be between -180 and 180, found %g",
                str_cstr(fx->name, buf, sizeof buf), fx->lon);
    }

    for (int i = 0; i < p->nprocs; i++) {
        s->cur = p->procs[i];
        check_block(s, &p->procs[i]->body);
    }
    s->cur = NULL;
    check_block(s, &p->main_body);

    /* ---- code-quality warnings (SPEC 7.5) ---- */

    for (int i = 0; i < st->all.count; i++) {
        Sym *sym = st->all.items[i];
        if (sym->used) continue;
        if (sym->kind == SYM_VAR)
            warn(s, sym->line, sym->col, "'%s' is declared but never read",
                 str_cstr(sym->name, buf, sizeof buf));
        else if (sym->kind == SYM_PARAM)
            warn(s, sym->line, sym->col, "parameter '%s' is never read",
                 str_cstr(sym->name, buf, sizeof buf));
        else if (sym->kind == SYM_PROC)
            warn(s, sym->line, sym->col, "procedure '%s' is never called",
                 str_cstr(sym->name, buf, sizeof buf));
        else if (sym->kind == SYM_FIX)
            warn(s, sym->line, sym->col, "waypoint '%s' is never used",
                 str_cstr(sym->name, buf, sizeof buf));
        else if (sym->kind == SYM_FLIGHT)
            warn(s, sym->line, sym->col,
                 "%s enters the airspace but nothing in the scenario refers to it again",
                 str_cstr(sym->name, buf, sizeof buf));
    }

    if (p->nflights == 0)
        warn(s, 1, 1, "this scenario declares no aircraft, so there is no traffic to simulate");

    if (!s->saw_advance && p->nflights > 0)
        warn(s, 1, 1,
             "this scenario never uses 'advance', so no simulated time passes and the aircraft "
             "never move");
}
