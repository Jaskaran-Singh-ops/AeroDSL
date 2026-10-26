/*
 * AeroDSL intermediate code generation (SPEC section 8).
 *
 * Translates the AST into three-address code, one Func per procedure plus
 * procedure 0 for the main body.
 *
 * Two things here are worth reading closely, because they are the heart of
 * the intermediate-code material:
 *
 * 1. Boolean expressions in a condition are translated into *jumping code*
 *    rather than into a value. `a and b` does not compute a truth value; it
 *    arranges the control flow so that b is only evaluated when a held. This
 *    is what makes `AI202.alt > 30000 and hsep(AI202, BA117) < 5` skip the
 *    expensive hsep call when the altitude test fails.
 *
 * 2. Because a jump is emitted before its target is known, each boolean
 *    carries a *truelist* and a *falselist* of instruction indices whose
 *    target is still blank, and the enclosing statement fills them in once
 *    it places the label. That is backpatching, and it is why the generator
 *    needs only a single forward pass.
 *
 * A comparison still has a value form (`t = a < b`), used when a boolean is
 * stored rather than branched on, as in `let converging = hsep(a,b) < 5`.
 * Both paths exist; which one runs depends on the context the expression
 * appears in.
 */
#include "aerodsl.h"

typedef struct {
    Func *fn;
    const Program *p;
    int next_label;
} G;

static const char *const IR_NAMES[] = {
    "const", "copy",  "bin",     "un",      "field",  "builtin", "label",
    "goto",  "if",    "ifFalse", "ifrel",   "enter",  "clear",   "advance",
    "assert", "print", "prints", "param",   "call",   "return",  "nop",
};

const char *ir_op_name(IrOp op) { return IR_NAMES[op]; }

/* ---- emission ------------------------------------------------------ */

static Arg a_none(void) { return (Arg){AR_NONE, 0, 0}; }
static Arg a_vreg(int id) { return (Arg){AR_VREG, id, 0}; }
static Arg a_const(double k) { return (Arg){AR_CONST, 0, k}; }
static Arg a_label(int id) { return (Arg){AR_LABEL, id, 0}; }

int ir_arg_eq(Arg a, Arg b) {
    if (a.kind != b.kind) return 0;
    switch (a.kind) {
    case AR_NONE: return 1;
    case AR_CONST: return a.k == b.k;
    default: return a.id == b.id;
    }
}

static int emit(G *g, Ir ir) {
    PUSH(g->fn->ir, ir);
    return g->fn->ir.count - 1;
}

static int new_label(G *g) { return g->next_label++; }

static void emit_label(G *g, int label) {
    emit(g, (Ir){IR_LABEL, 0, 0, a_label(label), a_none(), a_none(), 0, 0});
}

static int new_temp(G *g) { return g->fn->nvregs++; }

/* ---- backpatching (SPEC 8.3) --------------------------------------- *
 * A "list" is a list of indices of jump instructions whose target is not
 * yet decided. backpatch fills them all in with one label.
 */

static IntList makelist(int i) {
    IntList l = {0};
    intlist_push(&l, i);
    return l;
}

/* Appends b to a and frees b. */
static void merge(IntList *a, IntList *b) {
    for (int i = 0; i < b->count; i++) intlist_push(a, b->items[i]);
    intlist_free(b);
}

static void backpatch(G *g, IntList *list, int label) {
    for (int i = 0; i < list->count; i++) g->fn->ir.items[list->items[i]].dst = a_label(label);
    intlist_free(list);
}

/* ---- expressions (SPEC 8.2) ---------------------------------------- */

static Arg gen_expr(G *g, const Expr *e);

static Arg gen_sym(const Sym *s) {
    switch (s->kind) {
    case SYM_FLIGHT: return (Arg){AR_FLIGHT, s->id, (double)s->id};
    case SYM_FIX: return (Arg){AR_FIX, s->id, (double)s->id};
    default: return a_vreg(s->id);
    }
}

/* Materialises a boolean expression as a value rather than as control flow.
 * `and` and `or` keep their short-circuit behaviour by branching over the
 * second operand and assigning a constant on each path. */
static Arg gen_bool_value(G *g, const Expr *e);

static Arg gen_expr(G *g, const Expr *e) {
    if (!e) return a_const(0); /* an already-reported parse error */

    switch (e->kind) {
    case EX_NUM: return a_const(e->as.num);
    case EX_BOOL: return a_const(e->as.bval ? 1 : 0);
    case EX_SYM: return e->as.sym ? gen_sym(e->as.sym) : a_const(0);

    case EX_BIN:
        if (e->as.bin.op == OP_AND || e->as.bin.op == OP_OR) return gen_bool_value(g, e);
        {
            Arg l = gen_expr(g, e->as.bin.l);
            Arg r = gen_expr(g, e->as.bin.r);
            int t = new_temp(g);
            emit(g, (Ir){IR_BIN, e->as.bin.op, 0, a_vreg(t), l, r, e->line, e->col});
            return a_vreg(t);
        }

    case EX_UN: {
        Arg v = gen_expr(g, e->as.un.e);
        int t = new_temp(g);
        emit(g, (Ir){IR_UN, e->as.un.op, 0, a_vreg(t), v, a_none(), e->line, e->col});
        return a_vreg(t);
    }

    case EX_FIELD: {
        Arg obj = gen_expr(g, e->as.fld.obj);
        int t = new_temp(g);
        emit(g, (Ir){IR_FIELD, 0, (int)e->as.fld.field, a_vreg(t), obj, a_none(), e->line, e->col});
        return a_vreg(t);
    }

    case EX_BUILTIN: {
        Arg args[2] = {a_none(), a_none()};
        for (int i = 0; i < e->as.call.nargs && i < 2; i++) args[i] = gen_expr(g, e->as.call.args[i]);
        int t = new_temp(g);
        emit(g, (Ir){IR_BUILTIN, 0, (int)e->as.call.fn, a_vreg(t), args[0], args[1], e->line,
                     e->col});
        return a_vreg(t);
    }
    }
    return a_const(0);
}

/* ---- jumping code for conditions (SPEC 8.3) ------------------------ */

static void gen_cond(G *g, const Expr *e, IntList *truelist, IntList *falselist);

/* The fallback: evaluate the expression into a value, then branch on it. */
static void gen_cond_value(G *g, const Expr *e, IntList *truelist, IntList *falselist) {
    Arg v = gen_expr(g, e);
    int line = e ? e->line : 0, col = e ? e->col : 0;
    int jt = emit(g, (Ir){IR_IFTRUE, 0, 0, a_label(-1), v, a_none(), line, col});
    int jf = emit(g, (Ir){IR_GOTO, 0, 0, a_label(-1), a_none(), a_none(), line, col});
    *truelist = makelist(jt);
    *falselist = makelist(jf);
}

static void gen_cond(G *g, const Expr *e, IntList *truelist, IntList *falselist) {
    if (!e) {
        gen_cond_value(g, e, truelist, falselist);
        return;
    }

    if (e->kind == EX_BIN && e->as.bin.op == OP_AND) {
        /* B -> B1 and M B2:  B1 true falls through to B2, B1 false escapes. */
        IntList t1 = {0}, f1 = {0}, t2 = {0}, f2 = {0};
        gen_cond(g, e->as.bin.l, &t1, &f1);
        int m = new_label(g);
        backpatch(g, &t1, m);
        emit_label(g, m);
        gen_cond(g, e->as.bin.r, &t2, &f2);
        *truelist = t2;
        *falselist = f1;
        merge(falselist, &f2);
        return;
    }

    if (e->kind == EX_BIN && e->as.bin.op == OP_OR) {
        /* B -> B1 or M B2:  B1 false falls through to B2, B1 true escapes. */
        IntList t1 = {0}, f1 = {0}, t2 = {0}, f2 = {0};
        gen_cond(g, e->as.bin.l, &t1, &f1);
        int m = new_label(g);
        backpatch(g, &f1, m);
        emit_label(g, m);
        gen_cond(g, e->as.bin.r, &t2, &f2);
        *truelist = t1;
        merge(truelist, &t2);
        *falselist = f2;
        return;
    }

    if (e->kind == EX_UN && e->as.un.op == OP_NOT) {
        /* `not B` swaps the two lists and emits no code of its own. */
        gen_cond(g, e->as.un.e, falselist, truelist);
        return;
    }

    gen_cond_value(g, e, truelist, falselist);
}

static Arg gen_bool_value(G *g, const Expr *e) {
    IntList t = {0}, f = {0};
    gen_cond(g, e, &t, &f);
    int r = new_temp(g);

    int ltrue = new_label(g);
    backpatch(g, &t, ltrue);
    emit_label(g, ltrue);
    emit(g, (Ir){IR_CONST, 0, 0, a_vreg(r), a_const(1), a_none(), e->line, e->col});
    int over = emit(g, (Ir){IR_GOTO, 0, 0, a_label(-1), a_none(), a_none(), e->line, e->col});

    int lfalse = new_label(g);
    backpatch(g, &f, lfalse);
    emit_label(g, lfalse);
    emit(g, (Ir){IR_CONST, 0, 0, a_vreg(r), a_const(0), a_none(), e->line, e->col});

    int lend = new_label(g);
    IntList o = makelist(over);
    backpatch(g, &o, lend);
    emit_label(g, lend);
    return a_vreg(r);
}

/* ---- statements ---------------------------------------------------- */

static void gen_block(G *g, const StmtList *l);

static void gen_stmt(G *g, const Stmt *st) {
    switch (st->kind) {
    case ST_LET:
    case ST_ASSIGN: {
        Arg v = gen_expr(g, st->as.let.init);
        emit(g, (Ir){IR_COPY, 0, 0, a_vreg(st->as.let.sym->id), v, a_none(), st->line, st->col});
        break;
    }

    case ST_ENTER: {
        /* Three `param` instructions then `enter`, reusing the argument
         * mechanism rather than inventing a four-address instruction. */
        const FlightDecl *fl = &g->p->flights[st->as.enter.flight];
        const Expr *parts[3] = {fl->alt, fl->hdg, fl->spd};
        for (int i = 0; i < 3; i++) {
            Arg v = gen_expr(g, parts[i]);
            emit(g, (Ir){IR_PARAM, 0, 0, a_none(), v, a_none(), st->line, st->col});
        }
        emit(g, (Ir){IR_ENTER, 0, 0, a_none(),
                     (Arg){AR_FLIGHT, st->as.enter.flight, (double)st->as.enter.flight}, a_none(),
                     st->line, st->col});
        break;
    }

    case ST_CLEAR: {
        Arg f = gen_expr(g, st->as.clear.flight);
        Arg v = gen_expr(g, st->as.clear.operand);
        emit(g, (Ir){IR_CLEAR, 0, (int)st->as.clear.kind, a_none(), f, v, st->line, st->col});
        break;
    }

    case ST_ADVANCE: {
        Arg v = gen_expr(g, st->as.advance.secs);
        emit(g, (Ir){IR_ADVANCE, 0, 0, a_none(), v, a_none(), st->line, st->col});
        break;
    }

    case ST_IF: {
        IntList t = {0}, f = {0};
        gen_cond(g, st->as.ifs.cond, &t, &f);

        int ltrue = new_label(g);
        backpatch(g, &t, ltrue);
        emit_label(g, ltrue);
        gen_block(g, &st->as.ifs.then_body);

        if (st->as.ifs.has_else) {
            int over = emit(g, (Ir){IR_GOTO, 0, 0, a_label(-1), a_none(), a_none(), st->line,
                                    st->col});
            int lfalse = new_label(g);
            backpatch(g, &f, lfalse);
            emit_label(g, lfalse);
            gen_block(g, &st->as.ifs.else_body);
            int lend = new_label(g);
            IntList o = makelist(over);
            backpatch(g, &o, lend);
            emit_label(g, lend);
        } else {
            int lend = new_label(g);
            backpatch(g, &f, lend);
            emit_label(g, lend);
        }
        break;
    }

    case ST_WHILE: {
        int lbegin = new_label(g);
        emit_label(g, lbegin);

        IntList t = {0}, f = {0};
        gen_cond(g, st->as.whiles.cond, &t, &f);

        int lbody = new_label(g);
        backpatch(g, &t, lbody);
        emit_label(g, lbody);
        gen_block(g, &st->as.whiles.body);
        emit(g, (Ir){IR_GOTO, 0, 0, a_label(lbegin), a_none(), a_none(), st->line, st->col});

        int lafter = new_label(g);
        backpatch(g, &f, lafter);
        emit_label(g, lafter);
        break;
    }

    case ST_ASSERT: {
        /* The condition is materialised as a value: the VM has to record
         * whether it held, not merely branch on it. */
        Arg v = gen_expr(g, st->as.assertion.cond);
        Arg msg = st->as.assertion.msg >= 0 ? (Arg){AR_STR, st->as.assertion.msg, 0} : a_none();
        emit(g, (Ir){IR_ASSERT, 0, 0, a_none(), v, msg, st->line, st->col});
        break;
    }

    case ST_CALL: {
        if (!st->as.call.proc) break;
        Arg args[8];
        for (int i = 0; i < st->as.call.nargs; i++) args[i] = gen_expr(g, st->as.call.args[i]);
        for (int i = 0; i < st->as.call.nargs; i++)
            emit(g, (Ir){IR_PARAM, 0, 0, a_none(), args[i], a_none(), st->line, st->col});
        emit(g, (Ir){IR_CALL, 0, st->as.call.nargs, a_none(),
                     (Arg){AR_PROC, st->as.call.proc->id + 1, 0}, a_none(), st->line, st->col});
        break;
    }

    case ST_PRINT: {
        /* Every argument is evaluated first, so that the print instructions
         * themselves are contiguous. The VM gathers consecutive print
         * instructions into one line of output, and an evaluation
         * instruction in the middle would split that line in two. */
        Arg vals[8];
        for (int i = 0; i < st->as.print.nargs; i++)
            if (st->as.print.msgs[i] < 0) vals[i] = gen_expr(g, st->as.print.args[i]);

        int last = -1;
        for (int i = 0; i < st->as.print.nargs; i++) {
            if (st->as.print.msgs[i] >= 0) {
                last = emit(g, (Ir){IR_PRINTSTR, 0, 0, a_none(),
                                    (Arg){AR_STR, st->as.print.msgs[i], 0}, a_none(), st->line,
                                    st->col});
            } else {
                Type t = st->as.print.args[i] ? st->as.print.args[i]->type : TY_NUM;
                last = emit(g, (Ir){IR_PRINT, 0, (int)t, a_none(), vals[i], a_none(), st->line,
                                    st->col});
            }
        }
        /* `bop` is unused by the print instructions, so it carries the
         * end-of-statement marker: the VM joins the output of consecutive
         * print instructions into one line and ends the line here. Marking
         * it explicitly is necessary because register loads for the later
         * arguments sit between the print instructions in the bytecode. */
        if (last >= 0) g->fn->ir.items[last].bop = 1;
        break;
    }

    case ST_RETURN:
        emit(g, (Ir){IR_RETURN, 0, 0, a_none(), a_none(), a_none(), st->line, st->col});
        break;
    }
}

static void gen_block(G *g, const StmtList *l) {
    for (int i = 0; i < l->count; i++) gen_stmt(g, l->items[i]);
}

/* ---- virtual-register names ---------------------------------------- */

static void name_vreg(Func *fn, const Sym *s) {
    if (!s || s->id < 0 || s->id >= fn->nvregs) return;
    if (!fn->vnames) {
        fn->vnames = xcalloc((size_t)fn->nvregs, sizeof *fn->vnames);
        fn->nvnames = fn->nvregs;
    }
    fn->vnames[s->id] = s->name;
}

/* Walks the statements for `let` declarations, whose symbols carry the
 * source names of the virtual registers they were assigned. */
static void collect_names(Func *fn, const StmtList *l) {
    for (int i = 0; i < l->count; i++) {
        const Stmt *st = l->items[i];
        switch (st->kind) {
        case ST_LET: name_vreg(fn, st->as.let.sym); break;
        case ST_IF:
            collect_names(fn, &st->as.ifs.then_body);
            collect_names(fn, &st->as.ifs.else_body);
            break;
        case ST_WHILE: collect_names(fn, &st->as.whiles.body); break;
        default: break;
        }
    }
}

/* ---- entry point --------------------------------------------------- */

void ir_generate(const Program *p, FuncList *out) {
    memset(out, 0, sizeof *out);

    /* Func 0 is main; Func 1+i is p->procs[i], which is why a call's AR_PROC
     * id is the procedure index plus one. */
    Func main_fn = {0};
    main_fn.name = (Str){"main", 4};
    main_fn.nvregs = p->nvregs;
    PUSH(*out, main_fn);

    for (int i = 0; i < p->nprocs; i++) {
        Func f = {0};
        f.name = p->procs[i]->name;
        f.nparams = p->procs[i]->nparams;
        f.nvregs = p->procs[i]->nvregs;
        PUSH(*out, f);
    }

    /* Labels are numbered across the whole program so that a label id is
     * unique in a dump even when several functions are shown together. */
    int label = 0;
    for (int i = 0; i < out->count; i++) {
        G g = {&out->items[i], p, label};
        if (i == 0) {
            collect_names(g.fn, &p->main_body);
            gen_block(&g, &p->main_body);
        } else {
            const ProcDecl *pr = p->procs[i - 1];
            for (int j = 0; j < pr->nparams; j++) name_vreg(g.fn, pr->params[j]);
            collect_names(g.fn, &pr->body);
            gen_block(&g, &pr->body);
        }
        emit(&g, (Ir){IR_RETURN, 0, 0, a_none(), a_none(), a_none(), 0, 0});
        label = g.next_label;
    }
}

void funcs_free(FuncList *fns) {
    for (int i = 0; i < fns->count; i++) {
        Func *f = &fns->items[i];
        cfg_free(f);
        free(f->ir.items);
        free(f->vnames);
        free(f->bc);
    }
    free(fns->items);
    memset(fns, 0, sizeof *fns);
}

/* ---- printing ------------------------------------------------------ */

/* Prints a virtual register by its source name when it has one. */
void ir_print_vreg(FILE *f, const Func *fn, int id) {
    char buf[64];
    if (fn && id >= 0 && id < fn->nvnames && fn->vnames[id].ptr)
        fprintf(f, "%s", str_cstr(fn->vnames[id], buf, sizeof buf));
    else
        fprintf(f, "t%d", id);
}

static void print_arg(FILE *f, const Program *p, const Func *fn, Arg a) {
    char buf[64];
    switch (a.kind) {
    case AR_NONE: fputs("_", f); break;
    case AR_VREG: ir_print_vreg(f, fn, a.id); break;
    case AR_CONST: fprintf(f, "%g", a.k); break;
    case AR_LABEL: fprintf(f, "L%d", a.id); break;
    case AR_PROC: fprintf(f, "p%d", a.id); break;
    case AR_STR:
        if (p && a.id >= 0 && a.id < p->nstrings)
            fprintf(f, "\"%s\"", p->strings[a.id]);
        else
            fprintf(f, "s%d", a.id);
        break;
    case AR_FLIGHT:
        if (p && a.id >= 0 && a.id < p->nflights)
            fprintf(f, "%s", str_cstr(p->flights[a.id].name, buf, sizeof buf));
        else
            fprintf(f, "f%d", a.id);
        break;
    case AR_FIX:
        if (p && a.id >= 0 && a.id < p->nfixes)
            fprintf(f, "%s", str_cstr(p->fixes[a.id].name, buf, sizeof buf));
        else
            fprintf(f, "x%d", a.id);
        break;
    case AR_FIELD: fprintf(f, ".%s", field_name((Field)a.id)); break;
    case AR_CLEAR: fprintf(f, "%s", CLEARS[a.id].name); break;
    case AR_BI: fprintf(f, "%s", BUILTINS[a.id].name); break;
    case AR_INT: fprintf(f, "%d", a.id); break;
    }
}

/* The Program is optional: without it, indices are printed numerically. */
void ir_print_one(FILE *f, const Program *p, const Func *fn, const Ir *ir) {
#define A(x) print_arg(f, p, fn, (x))
    switch (ir->op) {
    case IR_LABEL:
        A(ir->dst);
        fputs(":", f);
        return;
    default: break;
    }
    fputs("    ", f);
    switch (ir->op) {
    case IR_CONST:
    case IR_COPY:
        A(ir->dst);
        fputs(" = ", f);
        A(ir->a);
        break;
    case IR_BIN:
        A(ir->dst);
        fputs(" = ", f);
        A(ir->a);
        fprintf(f, " %s ", op_symbol(ir->bop));
        A(ir->b);
        break;
    case IR_UN:
        A(ir->dst);
        fprintf(f, " = %s ", op_symbol(ir->bop));
        A(ir->a);
        break;
    case IR_FIELD:
        A(ir->dst);
        fputs(" = ", f);
        A(ir->a);
        fprintf(f, ".%s", field_name((Field)ir->aux));
        break;
    case IR_BUILTIN:
        A(ir->dst);
        fprintf(f, " = %s(", BUILTINS[ir->aux].name);
        if (BUILTINS[ir->aux].arity > 0) A(ir->a);
        if (BUILTINS[ir->aux].arity > 1) {
            fputs(", ", f);
            A(ir->b);
        }
        fputs(")", f);
        break;
    case IR_GOTO:
        fputs("goto ", f);
        A(ir->dst);
        break;
    case IR_IFTRUE:
        fputs("if ", f);
        A(ir->a);
        fputs(" goto ", f);
        A(ir->dst);
        break;
    case IR_IFFALSE:
        fputs("ifFalse ", f);
        A(ir->a);
        fputs(" goto ", f);
        A(ir->dst);
        break;
    case IR_IFREL:
        fputs("if ", f);
        A(ir->a);
        fprintf(f, " %s ", op_symbol(ir->bop));
        A(ir->b);
        fputs(" goto ", f);
        A(ir->dst);
        break;
    case IR_ENTER:
        fputs("enter ", f);
        A(ir->a);
        break;
    case IR_CLEAR:
        fputs("clear ", f);
        A(ir->a);
        fprintf(f, ", %s, ", CLEARS[ir->aux].name);
        A(ir->b);
        break;
    case IR_ADVANCE:
        fputs("advance ", f);
        A(ir->a);
        break;
    case IR_ASSERT:
        fputs("assert ", f);
        A(ir->a);
        if (ir->b.kind != AR_NONE) {
            fputs(", ", f);
            A(ir->b);
        }
        break;
    case IR_PRINT:
        fputs("print ", f);
        A(ir->a);
        break;
    case IR_PRINTSTR:
        fputs("print ", f);
        A(ir->a);
        break;
    case IR_PARAM:
        fputs("param ", f);
        A(ir->a);
        break;
    case IR_CALL:
        fputs("call ", f);
        A(ir->a);
        fprintf(f, ", %d", ir->aux);
        break;
    case IR_RETURN: fputs("return", f); break;
    case IR_NOP: fputs("nop", f); break;
    case IR_LABEL: break;
    }
#undef A
}

void ir_print(FILE *f, const Program *p, const Func *fn) {
    char buf[64];
    fprintf(f, "function %s(%d param%s), %d virtual registers\n",
            str_cstr(fn->name, buf, sizeof buf), fn->nparams, fn->nparams == 1 ? "" : "s",
            fn->nvregs);
    for (int i = 0; i < fn->ir.count; i++) {
        fprintf(f, "%4d  ", i);
        ir_print_one(f, p, fn, &fn->ir.items[i]);
        fputc('\n', f);
    }
}

void ir_print_all(FILE *f, const Program *p, const FuncList *fns) {
    for (int i = 0; i < fns->count; i++) {
        if (i) fputc('\n', f);
        ir_print(f, p, &fns->items[i]);
    }
}
