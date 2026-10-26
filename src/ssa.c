/*
 * Static single assignment (SPEC section 9.1).
 *
 * SSA is the form in which every register is assigned exactly once, so that
 * a use names its definition unambiguously. Where control flow joins and a
 * register could hold either of two definitions, a phi function names both,
 * and which one it takes depends on the edge control arrived along.
 *
 * Construction follows Cytron's algorithm:
 *
 *   1. Dominance frontiers. The frontier of a block n is the set of blocks
 *      that n does not strictly dominate but whose predecessors it does --
 *      exactly the places where a definition in n stops being the only one
 *      that can reach, and so exactly where a phi function is needed.
 *
 *   2. Phi placement, iterated to a fixed point: a phi is itself a
 *      definition, so it can force further phis on its own frontier.
 *
 *   3. Renaming, by a walk of the dominator tree carrying a stack of live
 *      versions per register.
 *
 * This module builds and prints the form; it does not rewrite the code the
 * rest of the compiler uses. The optimiser works on the ordinary
 * three-address code, and SPEC section 14 lists converting it to SSA as
 * work this project did not do.
 */
#include "aerodsl.h"

/* ---- dominance frontiers ------------------------------------------- */

BitSet *cfg_dominance_frontiers(const Func *fn) {
    if (fn->nblocks == 0) return NULL;
    BitSet *df = xcalloc((size_t)fn->nblocks, sizeof *df);
    for (int i = 0; i < fn->nblocks; i++) bs_init(&df[i], fn->nblocks);

    /* For every join point b and every predecessor p of b, walk up the
     * dominator tree from p until the immediate dominator of b is reached,
     * adding b to each block's frontier on the way. */
    for (int b = 0; b < fn->nblocks; b++) {
        if (!fn->blocks[b].reachable || fn->blocks[b].pred.count < 2) continue;
        for (int j = 0; j < fn->blocks[b].pred.count; j++) {
            int runner = fn->blocks[b].pred.items[j];
            int guard = 0;
            while (runner >= 0 && runner != fn->blocks[b].idom && guard++ < fn->nblocks + 2) {
                bs_set(&df[runner], b);
                runner = fn->blocks[runner].idom;
            }
        }
    }
    return df;
}

/* ---- phi placement and renaming ------------------------------------ */

typedef struct {
    int var;      /* the virtual register this phi defines */
    int version;  /* the version it defines */
    int *args;    /* one incoming version per predecessor of the block */
} Phi;

typedef struct {
    const Func *fn;
    int nv, nb;

    Phi *phis;       /* all phis, grouped by block */
    int *phi_start;  /* phi_start[b] .. phi_start[b+1] are block b's phis */
    int nphis;

    int *def_ver;    /* per instruction: the version its destination defines */
    int *use_ver;    /* per instruction: two incoming versions */

    int *counter;    /* next version number per register */
    IntList *stack;  /* live versions per register */
    int *children;   /* dominator tree, as a flattened child list */
    int *child_start;
} Ssa;

static int top_version(Ssa *s, int v) {
    IntList *st = &s->stack[v];
    return st->count ? st->items[st->count - 1] : 0; /* 0 means undefined */
}

static int new_version(Ssa *s, int v) {
    int ver = ++s->counter[v];
    intlist_push(&s->stack[v], ver);
    return ver;
}

static void place_phis(Ssa *s, const BitSet *df) {
    const Func *fn = s->fn;

    /* Which blocks assign each register. */
    char *defs_in = xcalloc((size_t)s->nb * s->nv, 1);
    for (int b = 0; b < s->nb; b++) {
        if (!fn->blocks[b].reachable) continue;
        for (int i = fn->blocks[b].first; i <= fn->blocks[b].last; i++) {
            int w = ir_writes(&fn->ir.items[i]);
            if (w >= 0) defs_in[(size_t)b * s->nv + w] = 1;
        }
    }

    char *needs = xcalloc((size_t)s->nb * s->nv, 1);
    IntList work = {0};
    for (int v = 0; v < s->nv; v++) {
        work.count = 0;
        for (int b = 0; b < s->nb; b++)
            if (defs_in[(size_t)b * s->nv + v]) intlist_push(&work, b);

        /* A phi is itself a definition, so placing one can force more. */
        while (work.count) {
            int b = work.items[--work.count];
            for (int d = 0; d < s->nb; d++) {
                if (!bs_get(&df[b], d)) continue;
                if (needs[(size_t)d * s->nv + v]) continue;
                /*
                 * Pruned SSA: a phi is only worth placing where the value
                 * is actually live on entry. Without this test the form is
                 * still correct but carries a phi for every block-local
                 * temporary that happens to sit in a loop, which is noise.
                 * The liveness this uses is the same analysis the optimiser
                 * and the register allocator run on.
                 */
                if (!bs_get(&fn->blocks[d].live_in, v)) continue;
                needs[(size_t)d * s->nv + v] = 1;
                if (!defs_in[(size_t)d * s->nv + v]) intlist_push(&work, d);
            }
        }
    }
    intlist_free(&work);
    free(defs_in);

    /* Flatten into one array grouped by block. */
    s->phi_start = xcalloc((size_t)s->nb + 1, sizeof *s->phi_start);
    int total = 0;
    for (int b = 0; b < s->nb; b++)
        for (int v = 0; v < s->nv; v++)
            if (needs[(size_t)b * s->nv + v]) total++;

    s->phis = xcalloc((size_t)(total > 0 ? total : 1), sizeof *s->phis);
    int at = 0;
    for (int b = 0; b < s->nb; b++) {
        s->phi_start[b] = at;
        for (int v = 0; v < s->nv; v++) {
            if (!needs[(size_t)b * s->nv + v]) continue;
            s->phis[at].var = v;
            s->phis[at].args = xcalloc((size_t)(fn->blocks[b].pred.count > 0
                                                    ? fn->blocks[b].pred.count
                                                    : 1),
                                       sizeof *s->phis[at].args);
            at++;
        }
    }
    s->phi_start[s->nb] = at;
    s->nphis = at;
    free(needs);
}

static void build_dom_tree(Ssa *s) {
    const Func *fn = s->fn;
    s->child_start = xcalloc((size_t)s->nb + 1, sizeof *s->child_start);
    int *count = xcalloc((size_t)s->nb, sizeof *count);
    for (int b = 1; b < s->nb; b++)
        if (fn->blocks[b].reachable && fn->blocks[b].idom >= 0) count[fn->blocks[b].idom]++;
    int at = 0;
    for (int b = 0; b < s->nb; b++) {
        s->child_start[b] = at;
        at += count[b];
    }
    s->child_start[s->nb] = at;
    s->children = xcalloc((size_t)(at > 0 ? at : 1), sizeof *s->children);
    int *fill = xcalloc((size_t)s->nb, sizeof *fill);
    for (int b = 1; b < s->nb; b++) {
        if (!fn->blocks[b].reachable || fn->blocks[b].idom < 0) continue;
        int p = fn->blocks[b].idom;
        s->children[s->child_start[p] + fill[p]++] = b;
    }
    free(count);
    free(fill);
}

static void rename_block(Ssa *s, int b) {
    const Func *fn = s->fn;
    int pushed_mark[512];
    int npushed = 0;

    /* Each phi defines a new version of its register. */
    for (int k = s->phi_start[b]; k < s->phi_start[b + 1]; k++) {
        s->phis[k].version = new_version(s, s->phis[k].var);
        if (npushed < LEN(pushed_mark)) pushed_mark[npushed++] = s->phis[k].var;
    }

    for (int i = fn->blocks[b].first; i <= fn->blocks[b].last; i++) {
        const Ir *ir = &fn->ir.items[i];
        int r[2], nr = ir_reads(ir, r);
        for (int k = 0; k < nr; k++) s->use_ver[(size_t)i * 2 + k] = top_version(s, r[k]);
        int w = ir_writes(ir);
        if (w >= 0) {
            s->def_ver[i] = new_version(s, w);
            if (npushed < LEN(pushed_mark)) pushed_mark[npushed++] = w;
        }
    }

    /* Fill in each successor's phi arguments for the edge from this block. */
    for (int j = 0; j < fn->blocks[b].succ.count; j++) {
        int sb = fn->blocks[b].succ.items[j];
        int which = -1;
        for (int q = 0; q < fn->blocks[sb].pred.count; q++)
            if (fn->blocks[sb].pred.items[q] == b) {
                which = q;
                break;
            }
        if (which < 0) continue;
        for (int k = s->phi_start[sb]; k < s->phi_start[sb + 1]; k++)
            s->phis[k].args[which] = top_version(s, s->phis[k].var);
    }

    for (int c = s->child_start[b]; c < s->child_start[b + 1]; c++)
        rename_block(s, s->children[c]);

    for (int k = npushed - 1; k >= 0; k--) {
        IntList *st = &s->stack[pushed_mark[k]];
        if (st->count) st->count--;
    }
}

/* ---- printing ------------------------------------------------------ */

/* An immediate operand, spelled the way the IR dump spells it. */
static void print_plain(FILE *f, const Program *p, const Func *fn, Arg a) {
    char buf[64];
    switch (a.kind) {
    case AR_CONST: fprintf(f, "%g", a.k); break;
    case AR_FLIGHT:
        fprintf(f, "%s", (p && a.id >= 0 && a.id < p->nflights)
                             ? str_cstr(p->flights[a.id].name, buf, sizeof buf)
                             : "flight?");
        break;
    case AR_FIX:
        fprintf(f, "%s", (p && a.id >= 0 && a.id < p->nfixes)
                             ? str_cstr(p->fixes[a.id].name, buf, sizeof buf)
                             : "fix?");
        break;
    case AR_STR: fprintf(f, "s%d", a.id); break;
    case AR_VREG: ir_print_vreg(f, fn, a.id); break;
    default: fputc('_', f); break;
    }
}

static void print_versioned(FILE *f, const Func *fn, int vreg, int version) {
    ir_print_vreg(f, fn, vreg);
    if (version > 0)
        fprintf(f, ".%d", version);
    else
        fputs(".undef", f);
}

void ssa_print(FILE *f, const Program *p, Func *fn) {
    char buf[64];
    fprintf(f, "static single assignment form of %s\n", str_cstr(fn->name, buf, sizeof buf));
    if (fn->nblocks == 0 || fn->nvregs == 0) {
        fputs("  nothing to show: the function has no registers or no blocks\n", f);
        return;
    }

    /* Phi placement and the dominator-tree walk both need immediate
     * dominators, and an earlier phase may have rebuilt the graph without
     * recomputing them, so they are computed here rather than assumed. */
    cfg_dominators(fn);
    cfg_liveness(fn); /* pruned SSA needs it, see place_phis */

    BitSet *df = cfg_dominance_frontiers(fn);

    Ssa s = {0};
    s.fn = fn;
    s.nv = fn->nvregs;
    s.nb = fn->nblocks;
    s.def_ver = xcalloc((size_t)fn->ir.count, sizeof *s.def_ver);
    s.use_ver = xcalloc((size_t)fn->ir.count * 2, sizeof *s.use_ver);
    s.counter = xcalloc((size_t)s.nv, sizeof *s.counter);
    s.stack = xcalloc((size_t)s.nv, sizeof *s.stack);

    place_phis(&s, df);
    build_dom_tree(&s);

    /* Parameters arrive already defined, as version 1. */
    for (int v = 0; v < fn->nparams && v < s.nv; v++) new_version(&s, v);

    rename_block(&s, 0);

    fputs("\ndominance frontiers\n", f);
    for (int b = 0; b < s.nb; b++) {
        fprintf(f, "  B%-3d", b);
        int any = 0;
        for (int d = 0; d < s.nb; d++)
            if (bs_get(&df[b], d)) {
                fprintf(f, " B%d", d);
                any = 1;
            }
        if (!any) fputs(" none", f);
        fputc('\n', f);
    }

    fprintf(f, "\n%d phi function%s inserted (pruned: a phi is placed only where the value "
               "is live on entry)\n\n",
            s.nphis, s.nphis == 1 ? "" : "s");

    for (int b = 0; b < s.nb; b++) {
        fprintf(f, "B%d:%s\n", b, fn->blocks[b].reachable ? "" : "   [unreachable]");

        for (int k = s.phi_start[b]; k < s.phi_start[b + 1]; k++) {
            const Phi *phi = &s.phis[k];
            fputs("      ", f);
            print_versioned(f, fn, phi->var, phi->version);
            fputs(" = phi(", f);
            for (int q = 0; q < fn->blocks[b].pred.count; q++) {
                if (q) fputs(", ", f);
                print_versioned(f, fn, phi->var, phi->args[q]);
                fprintf(f, " from B%d", fn->blocks[b].pred.items[q]);
            }
            fputs(")\n", f);
        }

        for (int i = fn->blocks[b].first; i <= fn->blocks[b].last; i++) {
            const Ir *ir = &fn->ir.items[i];
            fprintf(f, "  %4d  ", i);

            int w = ir_writes(ir);
            int r[2], nr = ir_reads(ir, r);
            int k = 0; /* how many of the reads have been consumed */
            Arg ops[2] = {ir->a, ir->b};

            /* An operand is printed as a version when it is a register and
             * as itself when it is an immediate. */
            #define OPERAND(which)                                            \
                do {                                                          \
                    if (ops[which].kind == AR_VREG && k < nr) {               \
                        print_versioned(f, fn, r[k], s.use_ver[(size_t)i * 2 + k]); \
                        k++;                                                  \
                    } else {                                                  \
                        print_plain(f, p, fn, ops[which]);                    \
                    }                                                         \
                } while (0)

            if (w >= 0) {
                print_versioned(f, fn, w, s.def_ver[i]);
                fputs(" = ", f);
            }

            switch (ir->op) {
            case IR_CONST:
            case IR_COPY: OPERAND(0); break;
            case IR_BIN:
                OPERAND(0);
                fprintf(f, " %s ", op_symbol(ir->bop));
                OPERAND(1);
                break;
            case IR_UN:
                fprintf(f, "%s ", op_symbol(ir->bop));
                OPERAND(0);
                break;
            case IR_FIELD:
                OPERAND(0);
                fprintf(f, ".%s", field_name((Field)ir->aux));
                break;
            case IR_BUILTIN:
                fprintf(f, "%s(", BUILTINS[ir->aux].name);
                if (BUILTINS[ir->aux].arity > 0) OPERAND(0);
                if (BUILTINS[ir->aux].arity > 1) {
                    fputs(", ", f);
                    OPERAND(1);
                }
                fputc(')', f);
                break;
            case IR_CLEAR:
                fputs("clear ", f);
                OPERAND(0);
                fprintf(f, ", %s, ", CLEARS[ir->aux].name);
                OPERAND(1);
                break;
            case IR_ADVANCE:
                fputs("advance ", f);
                OPERAND(0);
                break;
            case IR_PARAM:
                fputs("param ", f);
                OPERAND(0);
                break;
            case IR_PRINT:
                fputs("print ", f);
                OPERAND(0);
                break;
            case IR_IFTRUE:
                fputs("if ", f);
                OPERAND(0);
                fprintf(f, " goto L%d", ir->dst.id);
                break;
            case IR_IFFALSE:
                fputs("ifFalse ", f);
                OPERAND(0);
                fprintf(f, " goto L%d", ir->dst.id);
                break;
            case IR_IFREL:
                fputs("if ", f);
                OPERAND(0);
                fprintf(f, " %s ", op_symbol(ir->bop));
                OPERAND(1);
                fprintf(f, " goto L%d", ir->dst.id);
                break;
            case IR_ASSERT:
                fputs("assert ", f);
                OPERAND(0);
                break;
            default:
                /* Instructions with no register operands print as they are. */
                ir_print_one(f, p, fn, ir);
                break;
            }
            #undef OPERAND
            fputc('\n', f);
        }
    }

    for (int i = 0; i < s.nphis; i++) free(s.phis[i].args);
    free(s.phis);
    free(s.phi_start);
    free(s.def_ver);
    free(s.use_ver);
    free(s.counter);
    for (int v = 0; v < s.nv; v++) intlist_free(&s.stack[v]);
    free(s.stack);
    free(s.children);
    free(s.child_start);
    for (int i = 0; i < fn->nblocks; i++) bs_free(&df[i]);
    free(df);
}
