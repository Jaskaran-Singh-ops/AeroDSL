/*
 * AST construction helpers, the domain tables that several phases share,
 * and an indented tree printer used by the CLI and the demo UI.
 */
#include "aerodsl.h"

/* ---- shared tables ------------------------------------------------ */

static const char *const TYPE_NAMES[] = {"<error>", "num", "bool", "flight", "fix", "void"};
const char *type_name(Type t) { return TYPE_NAMES[t]; }

static const char *const OP_SYMBOLS[] = {"+",  "-",  "*",   "/",  "%",  "<",  "<=",  ">",
                                         ">=", "==", "!=", "and", "or", "-", "not"};
const char *op_symbol(BinOp op) { return OP_SYMBOLS[op]; }

static const char *const FIELD_NAMES[] = {"alt", "hdg", "spd", "lat", "lon"};
const char *field_name(Field f) { return FIELD_NAMES[f]; }

/* SPEC section 5.3. `state` built-ins read the simulated world and so are
 * barriers for the optimiser (SPEC 10.6); the pure ones are foldable. */
const BuiltinInfo BUILTINS[BI_COUNT] = {
    [BI_VSEP] = {"vsep", 2, TY_NUM, {TY_FLIGHT, TY_FLIGHT}, "ft", "vertical separation"},
    [BI_HSEP] = {"hsep", 2, TY_NUM, {TY_FLIGHT, TY_FLIGHT}, "NM", "horizontal separation"},
    [BI_DIST] = {"dist", 2, TY_NUM, {TY_FLIGHT, TY_FIX}, "NM", "distance to a waypoint"},
    [BI_BEARING] = {"bearing", 2, TY_NUM, {TY_FLIGHT, TY_FIX}, "deg", "bearing to a waypoint"},
    [BI_ABS] = {"abs", 1, TY_NUM, {TY_NUM, TY_ERROR}, "", "absolute value"},
    [BI_MIN] = {"min", 2, TY_NUM, {TY_NUM, TY_NUM}, "", "smaller of two numbers"},
    [BI_MAX] = {"max", 2, TY_NUM, {TY_NUM, TY_NUM}, "", "larger of two numbers"},
    [BI_NOW] = {"now", 0, TY_NUM, {TY_ERROR, TY_ERROR}, "s", "elapsed simulation time"},
};

/* SPEC sections 7.2 and 7.3. */
const ClearInfo CLEARS[CL_COUNT] = {
    [CL_CLIMB] = {"climb", TY_NUM, "ft", 0, 60000},
    [CL_DESCEND] = {"descend", TY_NUM, "ft", 0, 60000},
    [CL_HEADING] = {"heading", TY_NUM, "deg", 0, 360},
    [CL_SPEED] = {"speed", TY_NUM, "kt", 60, 600},
    [CL_DIRECT] = {"direct", TY_FIX, "", 0, 0},
};

/* ---- constructors ------------------------------------------------- */

Expr *expr_new(ExprKind kind, int line, int col) {
    Expr *e = xcalloc(1, sizeof *e);
    e->kind = kind;
    e->type = TY_ERROR;
    e->line = line;
    e->col = col;
    return e;
}

Stmt *stmt_new(StmtKind kind, int line, int col) {
    Stmt *s = xcalloc(1, sizeof *s);
    s->kind = kind;
    s->line = line;
    s->col = col;
    return s;
}

void stmtlist_push(StmtList *l, Stmt *s) { PUSH(*l, s); }

int program_add_string(Program *p, Str s) {
    char *copy = xmalloc((size_t)s.len + 1);
    memcpy(copy, s.ptr, (size_t)s.len);
    copy[s.len] = '\0';
    PUSHN(p->strings, p->nstrings, p->strings_cap, copy);
    return p->nstrings - 1;
}

/* ---- teardown ----------------------------------------------------- */

void expr_free(Expr *e) {
    if (!e) return;
    switch (e->kind) {
    case EX_BIN:
        expr_free(e->as.bin.l);
        expr_free(e->as.bin.r);
        break;
    case EX_UN: expr_free(e->as.un.e); break;
    case EX_FIELD: expr_free(e->as.fld.obj); break;
    case EX_BUILTIN:
        for (int i = 0; i < e->as.call.nargs; i++) expr_free(e->as.call.args[i]);
        break;
    default: break;
    }
    free(e);
}

static void stmtlist_free(StmtList *l);

static void stmt_free(Stmt *s) {
    if (!s) return;
    switch (s->kind) {
    case ST_LET:
    case ST_ASSIGN: expr_free(s->as.let.init); break;
    case ST_ENTER: break; /* the expressions belong to the FlightDecl */
    case ST_CLEAR:
        expr_free(s->as.clear.flight);
        expr_free(s->as.clear.operand);
        break;
    case ST_ADVANCE: expr_free(s->as.advance.secs); break;
    case ST_IF:
        expr_free(s->as.ifs.cond);
        stmtlist_free(&s->as.ifs.then_body);
        stmtlist_free(&s->as.ifs.else_body);
        break;
    case ST_WHILE:
        expr_free(s->as.whiles.cond);
        stmtlist_free(&s->as.whiles.body);
        break;
    case ST_ASSERT: expr_free(s->as.assertion.cond); break;
    case ST_CALL:
        for (int i = 0; i < s->as.call.nargs; i++) expr_free(s->as.call.args[i]);
        break;
    case ST_PRINT:
        for (int i = 0; i < s->as.print.nargs; i++) expr_free(s->as.print.args[i]);
        break;
    case ST_RETURN: break;
    }
    free(s);
}

static void stmtlist_free(StmtList *l) {
    for (int i = 0; i < l->count; i++) stmt_free(l->items[i]);
    free(l->items);
    l->items = NULL;
    l->count = l->cap = 0;
}

void program_free(Program *p) {
    for (int i = 0; i < p->nflights; i++) {
        expr_free(p->flights[i].alt);
        expr_free(p->flights[i].hdg);
        expr_free(p->flights[i].spd);
    }
    free(p->flights);
    free(p->fixes);
    for (int i = 0; i < p->nprocs; i++) {
        stmtlist_free(&p->procs[i]->body);
        free(p->procs[i]);
    }
    free(p->procs);
    stmtlist_free(&p->main_body);
    for (int i = 0; i < p->nstrings; i++) free(p->strings[i]);
    free(p->strings);
    memset(p, 0, sizeof *p);
}

/* ---- printing ----------------------------------------------------- */

static void indent(FILE *f, int depth) {
    for (int i = 0; i < depth; i++) fputs("  ", f);
}

static void print_expr(FILE *f, const Expr *e, int depth) {
    indent(f, depth);
    if (!e) {
        fputs("<missing>\n", f);
        return;
    }
    char buf[64];
    switch (e->kind) {
    case EX_NUM: fprintf(f, "Num %g : %s\n", e->as.num, type_name(e->type)); break;
    case EX_BOOL: fprintf(f, "Bool %s : %s\n", e->as.bval ? "true" : "false", type_name(e->type)); break;
    case EX_SYM:
        /* sym is NULL when the name did not resolve; the error is already
         * reported, and the tree is still printed so the rest is visible. */
        fprintf(f, "Name %s : %s\n",
                e->as.sym ? str_cstr(e->as.sym->name, buf, sizeof buf) : "<unresolved>",
                type_name(e->type));
        break;
    case EX_BIN:
        fprintf(f, "Binary '%s' : %s\n", op_symbol(e->as.bin.op), type_name(e->type));
        print_expr(f, e->as.bin.l, depth + 1);
        print_expr(f, e->as.bin.r, depth + 1);
        break;
    case EX_UN:
        fprintf(f, "Unary '%s' : %s\n", op_symbol(e->as.un.op), type_name(e->type));
        print_expr(f, e->as.un.e, depth + 1);
        break;
    case EX_FIELD:
        fprintf(f, "Field .%s : %s\n", field_name(e->as.fld.field), type_name(e->type));
        print_expr(f, e->as.fld.obj, depth + 1);
        break;
    case EX_BUILTIN:
        fprintf(f, "Builtin %s() : %s\n", BUILTINS[e->as.call.fn].name, type_name(e->type));
        for (int i = 0; i < e->as.call.nargs; i++) print_expr(f, e->as.call.args[i], depth + 1);
        break;
    }
}

static void print_stmts(FILE *f, const Program *p, const StmtList *l, int depth);

static void print_stmt(FILE *f, const Program *p, const Stmt *s, int depth) {
    indent(f, depth);
    char buf[64];
    switch (s->kind) {
    case ST_LET:
        fprintf(f, "Let %s\n", str_cstr(s->as.let.sym->name, buf, sizeof buf));
        print_expr(f, s->as.let.init, depth + 1);
        break;
    case ST_ASSIGN:
        fprintf(f, "Assign %s\n", str_cstr(s->as.let.sym->name, buf, sizeof buf));
        print_expr(f, s->as.let.init, depth + 1);
        break;
    case ST_ENTER: {
        const FlightDecl *fl = &p->flights[s->as.enter.flight];
        char fixbuf[64];
        fprintf(f, "Enter %s at %s\n", str_cstr(fl->name, buf, sizeof buf),
                fl->fix >= 0 ? str_cstr(p->fixes[fl->fix].name, fixbuf, sizeof fixbuf)
                             : "<unresolved>");
        indent(f, depth + 1);
        fputs("alt:\n", f);
        print_expr(f, fl->alt, depth + 2);
        indent(f, depth + 1);
        fputs("hdg:\n", f);
        print_expr(f, fl->hdg, depth + 2);
        indent(f, depth + 1);
        fputs("spd:\n", f);
        print_expr(f, fl->spd, depth + 2);
        break;
    }
    case ST_CLEAR:
        fprintf(f, "Clear %s\n", CLEARS[s->as.clear.kind].name);
        print_expr(f, s->as.clear.flight, depth + 1);
        print_expr(f, s->as.clear.operand, depth + 1);
        break;
    case ST_ADVANCE:
        fputs("Advance\n", f);
        print_expr(f, s->as.advance.secs, depth + 1);
        break;
    case ST_IF:
        fputs("If\n", f);
        indent(f, depth + 1);
        fputs("cond:\n", f);
        print_expr(f, s->as.ifs.cond, depth + 2);
        indent(f, depth + 1);
        fputs("then:\n", f);
        print_stmts(f, p, &s->as.ifs.then_body, depth + 2);
        if (s->as.ifs.has_else) {
            indent(f, depth + 1);
            fputs("else:\n", f);
            print_stmts(f, p, &s->as.ifs.else_body, depth + 2);
        }
        break;
    case ST_WHILE:
        fputs("While\n", f);
        indent(f, depth + 1);
        fputs("cond:\n", f);
        print_expr(f, s->as.whiles.cond, depth + 2);
        indent(f, depth + 1);
        fputs("body:\n", f);
        print_stmts(f, p, &s->as.whiles.body, depth + 2);
        break;
    case ST_ASSERT:
        if (s->as.assertion.msg >= 0)
            fprintf(f, "Assert \"%s\"\n", p->strings[s->as.assertion.msg]);
        else
            fputs("Assert\n", f);
        print_expr(f, s->as.assertion.cond, depth + 1);
        break;
    case ST_CALL:
        fprintf(f, "Call %s\n",
                s->as.call.proc ? str_cstr(s->as.call.proc->name, buf, sizeof buf)
                                : "<unresolved>");
        for (int i = 0; i < s->as.call.nargs; i++) print_expr(f, s->as.call.args[i], depth + 1);
        break;
    case ST_PRINT:
        fputs("Print\n", f);
        for (int i = 0; i < s->as.print.nargs; i++) {
            if (s->as.print.msgs[i] >= 0) {
                indent(f, depth + 1);
                fprintf(f, "Str \"%s\"\n", p->strings[s->as.print.msgs[i]]);
            } else {
                print_expr(f, s->as.print.args[i], depth + 1);
            }
        }
        break;
    case ST_RETURN: fputs("Return\n", f); break;
    }
}

static void print_stmts(FILE *f, const Program *p, const StmtList *l, int depth) {
    if (l->count == 0) {
        indent(f, depth);
        fputs("<empty>\n", f);
    }
    for (int i = 0; i < l->count; i++) print_stmt(f, p, l->items[i], depth);
}

void ast_print(FILE *f, const Program *p) {
    char buf[128];
    fprintf(f, "Scenario \"%s\"\n", str_cstr(p->scenario, buf, sizeof buf));

    for (int i = 0; i < p->nfixes; i++)
        fprintf(f, "  Fix %s at %.4f %.4f\n", str_cstr(p->fixes[i].name, buf, sizeof buf),
                p->fixes[i].lat, p->fixes[i].lon);

    /* The initial state belongs to the Enter statement in the main body,
     * which is where the aircraft actually enters the airspace. */
    for (int i = 0; i < p->nflights; i++)
        fprintf(f, "  Flight %s (index %d)\n", str_cstr(p->flights[i].name, buf, sizeof buf), i);

    for (int i = 0; i < p->nprocs; i++) {
        const ProcDecl *pr = p->procs[i];
        fprintf(f, "  Proc %s(", str_cstr(pr->name, buf, sizeof buf));
        for (int j = 0; j < pr->nparams; j++)
            fprintf(f, "%s%s %s", j ? ", " : "", type_name(pr->params[j]->type),
                    str_cstr(pr->params[j]->name, buf, sizeof buf));
        fputs(")\n", f);
        print_stmts(f, p, &pr->body, 2);
    }

    fputs("  Main\n", f);
    print_stmts(f, p, &p->main_body, 2);
}
