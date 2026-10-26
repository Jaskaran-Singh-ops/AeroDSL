/*
 * AeroDSL code generation (SPEC section 11).
 *
 * Three steps:
 *
 *   1. Compare-and-branch fusion. `t = a < b` immediately consumed by a
 *      branch on t, with t dead afterwards, becomes the single three-address
 *      instruction `if a < b goto L`. This is done here rather than on the
 *      bytecode because the liveness information that proves t is dead is
 *      computed on the three-address code.
 *
 *   2. Next-use information and register allocation. AeroVM has 16 registers
 *      per frame; every virtual register also has a home slot in the frame,
 *      the slot of vreg v being v, which is what puts a procedure's
 *      parameters where the caller writes them. The allocator is the
 *      textbook on-demand scheme: reuse a register that holds a dead value,
 *      and when none is free spill the one whose value is needed furthest in
 *      the future. A register is only written back to its slot if it is
 *      dirty, so a value that was loaded and never changed costs no store.
 *
 *      Allocation is local to a basic block, so every live value is written
 *      back at the end of a block and re-loaded in the next. That is the
 *      price of not building an interference graph, and SPEC section 14
 *      lists it as a limitation.
 *
 *   3. Bytecode emission and a peephole pass over the result.
 */
#include <limits.h>

#include "aerodsl.h"

#define NO_USE INT_MAX

static const char *const BC_NAMES[] = {
    "loadk", "move",  "bin",    "un",     "load",  "store", "jmp",
    "jt",    "jf",    "jrel",   "field",  "bi",    "enter", "clear",
    "adv",   "assert", "print", "prints", "arg",   "call",  "ret",
    "halt",
};

const char *bc_op_name(BcOp op) { return BC_NAMES[op]; }

/* ---- step 1: compare-and-branch fusion ----------------------------- */

static BinOp invert_rel(BinOp op) {
    switch (op) {
    case OP_LT: return OP_GE;
    case OP_LE: return OP_GT;
    case OP_GT: return OP_LE;
    case OP_GE: return OP_LT;
    case OP_EQ: return OP_NE;
    case OP_NE: return OP_EQ;
    default: return op;
    }
}

static int is_rel(BinOp op) {
    switch (op) {
    case OP_LT:
    case OP_LE:
    case OP_GT:
    case OP_GE:
    case OP_EQ:
    case OP_NE: return 1;
    default: return 0;
    }
}

/* Is v read anywhere after index i inside the same block, or live out? */
static int needed_after(const Func *fn, int block, int i, int v) {
    for (int j = i + 1; j <= fn->blocks[block].last; j++) {
        int r[2], nr = ir_reads(&fn->ir.items[j], r);
        for (int k = 0; k < nr; k++)
            if (r[k] == v) return 1;
        if (ir_writes(&fn->ir.items[j]) == v) return 0; /* redefined, so not this value */
    }
    return bs_get(&fn->blocks[block].live_out, v);
}

/* Drops the nops left behind by fusion, so the instruction list the demo
 * shows is the one the bytecode is generated from. */
static void compact_nops(Func *fn) {
    IrList out = {0};
    int removed = 0;
    for (int i = 0; i < fn->ir.count; i++) {
        if (fn->ir.items[i].op == IR_NOP) {
            removed++;
            continue;
        }
        PUSH(out, fn->ir.items[i]);
    }
    if (!removed) {
        free(out.items);
        return;
    }
    free(fn->ir.items);
    fn->ir = out;
    cfg_build(fn);
}

static int fuse_compares(Func *fn, OptStats *stats) {
    int n = 0;
    for (int b = 0; b < fn->nblocks; b++) {
        if (!fn->blocks[b].reachable) continue;
        for (int i = fn->blocks[b].first; i < fn->blocks[b].last; i++) {
            Ir *cmp = &fn->ir.items[i];
            Ir *br = &fn->ir.items[i + 1];
            if (cmp->op != IR_BIN || !is_rel(cmp->bop)) continue;
            if (br->op != IR_IFTRUE && br->op != IR_IFFALSE) continue;
            if (cmp->dst.kind != AR_VREG || br->a.kind != AR_VREG) continue;
            if (cmp->dst.id != br->a.id) continue;
            if (needed_after(fn, b, i + 1, cmp->dst.id)) continue;

            br->bop = br->op == IR_IFTRUE ? cmp->bop : invert_rel(cmp->bop);
            br->op = IR_IFREL;
            br->a = cmp->a;
            br->b = cmp->b;
            cmp->op = IR_NOP;
            cmp->a = cmp->b = (Arg){AR_NONE, 0, 0};
            cmp->dst = (Arg){AR_NONE, 0, 0};
            n++;
            if (stats) stats->peephole++;
        }
    }
    return n;
}

/* ---- step 2: next-use information ---------------------------------- */

/*
 * nu[i * nv + v] is the index of the next instruction after i, within the
 * same block, that reads v -- or NO_USE if v is not read again in the block.
 * Computed by one backward scan per block: at each instruction, record what
 * is pending, then kill the destination (an earlier instruction's view of
 * that register refers to a different value) and record the reads.
 */
static int *compute_nextuse(const Func *fn) {
    int nv = fn->nvregs > 0 ? fn->nvregs : 1;
    int *nu = xmalloc((size_t)fn->ir.count * nv * sizeof *nu);
    int *last = xmalloc((size_t)nv * sizeof *last);

    for (int b = 0; b < fn->nblocks; b++) {
        for (int v = 0; v < nv; v++) last[v] = NO_USE;
        for (int i = fn->blocks[b].last; i >= fn->blocks[b].first; i--) {
            for (int v = 0; v < nv; v++) nu[(size_t)i * nv + v] = last[v];
            int w = ir_writes(&fn->ir.items[i]);
            if (w >= 0) last[w] = NO_USE;
            int r[2], nr = ir_reads(&fn->ir.items[i], r);
            for (int k = 0; k < nr; k++) last[r[k]] = i;
        }
    }
    free(last);
    return nu;
}

void nextuse_print(FILE *f, const Func *fn) {
    char buf[64];
    int nv = fn->nvregs > 0 ? fn->nvregs : 1;
    int *nu = compute_nextuse(fn);
    fprintf(f, "next-use information for %s\n", str_cstr(fn->name, buf, sizeof buf));
    for (int b = 0; b < fn->nblocks; b++) {
        fprintf(f, "  B%d\n", b);
        for (int i = fn->blocks[b].first; i <= fn->blocks[b].last; i++) {
            fprintf(f, "  %4d  ", i);
            int any = 0;
            for (int v = 0; v < nv; v++) {
                int u = nu[(size_t)i * nv + v];
                if (u == NO_USE) continue;
                if (any) fputs(", ", f);
                ir_print_vreg(f, fn, v);
                fprintf(f, " next used at %d", u);
                any = 1;
            }
            if (!any) fputs("nothing live forward in this block", f);
            fputc('\n', f);
        }
    }
    free(nu);
}

/* ---- step 2: register allocation ----------------------------------- */

/* A jump whose bytecode target is not known until every label is placed. */
typedef struct {
    int at, label;
} Fixup;

typedef struct {
    Func *fn;
    const Program *p;
    int nv;
    int *nu;             /* next-use table */
    int reg_holds[NREGS]; /* vreg in each register, or -1 */
    int dirty[NREGS];     /* written since being loaded */
    int pinned[NREGS];    /* in use for the instruction being emitted */
    int *vreg_in;         /* register holding each vreg, or -1 */
    int block;
    int argn;             /* how many arguments have been staged */
    Fixup *fix;
    int nfix, fix_cap;
} C;

static int bc_emit(C *c, Bc b) {
    PUSHN(c->fn->bc, c->fn->nbc, c->fn->bc_cap, b);
    return c->fn->nbc - 1;
}

static void bc_jump(C *c, BcOp op, int label, int bop, int rb, int rc, int line, int col) {
    int at = bc_emit(c, (Bc){op, (BinOp)bop, 0, -1, rb, rc, 0, line, col});
    PUSHN(c->fix, c->nfix, c->fix_cap, ((Fixup){at, label}));
}

/* Is v still needed after instruction i? */
static int live_after(C *c, int i, int v) {
    if (c->nu[(size_t)i * c->nv + v] != NO_USE) return 1;
    return bs_get(&c->fn->blocks[c->block].live_out, v);
}

static void spill(C *c, int r, int i) {
    int v = c->reg_holds[r];
    if (v >= 0 && c->dirty[r] && live_after(c, i, v))
        bc_emit(c, (Bc){BC_STORE, 0, 0, v, r, 0, 0, 0, 0});
    if (v >= 0) c->vreg_in[v] = -1;
    c->reg_holds[r] = -1;
    c->dirty[r] = 0;
}

/*
 * Obtains a register for use at instruction i: an empty one, then one whose
 * value is already dead, and only then a spill of the value needed furthest
 * in the future. That last rule is what next-use information is for.
 */
static int getreg(C *c, int i) {
    for (int r = 0; r < NREGS; r++)
        if (!c->pinned[r] && c->reg_holds[r] < 0) return r;

    for (int r = 0; r < NREGS; r++) {
        if (c->pinned[r]) continue;
        int v = c->reg_holds[r];
        if (v >= 0 && !live_after(c, i, v)) {
            c->vreg_in[v] = -1;
            c->reg_holds[r] = -1;
            c->dirty[r] = 0;
            return r;
        }
    }

    int best = -1, best_use = -1;
    for (int r = 0; r < NREGS; r++) {
        if (c->pinned[r]) continue;
        int v = c->reg_holds[r];
        int u = v >= 0 ? c->nu[(size_t)i * c->nv + v] : NO_USE;
        if (u > best_use) {
            best_use = u;
            best = r;
        }
    }
    if (best < 0) best = 0; /* cannot happen: at most two pins per instruction */
    spill(c, best, i);
    return best;
}

/* Puts the value of an operand into a register and returns it. */
static int in_reg(C *c, Arg a, int i) {
    if (a.kind == AR_VREG) {
        int v = a.id;
        if (c->vreg_in[v] >= 0) {
            c->pinned[c->vreg_in[v]] = 1;
            return c->vreg_in[v];
        }
        int r = getreg(c, i);
        bc_emit(c, (Bc){BC_LOAD, 0, 0, r, v, 0, 0, 0, 0});
        c->reg_holds[r] = v;
        c->vreg_in[v] = r;
        c->dirty[r] = 0;
        c->pinned[r] = 1;
        return r;
    }
    /* An immediate: load it into a scratch register bound to no vreg. */
    double k = a.kind == AR_CONST ? a.k : (double)a.id;
    int r = getreg(c, i);
    bc_emit(c, (Bc){BC_LOADK, 0, 0, r, 0, 0, k, 0, 0});
    c->reg_holds[r] = -1;
    c->dirty[r] = 0;
    c->pinned[r] = 1;
    return r;
}

static void unpin_all(C *c) { memset(c->pinned, 0, sizeof c->pinned); }

/*
 * Picks the register the result of instruction i is written to.
 *
 * The preference order matters for code quality: overwrite the destination's
 * own register if it already has one, then reuse an operand register whose
 * value dies at this instruction -- safe because the VM reads both sources
 * before writing the destination -- and only then take a fresh register.
 */
static int dest_reg(C *c, int v, int i) {
    if (c->vreg_in[v] >= 0) {
        int r = c->vreg_in[v];
        c->dirty[r] = 1;
        return r;
    }

    for (int r = 0; r < NREGS; r++) {
        if (!c->pinned[r]) continue;
        int ov = c->reg_holds[r];
        if (ov >= 0 && live_after(c, i, ov)) continue;
        if (ov >= 0) c->vreg_in[ov] = -1;
        c->reg_holds[r] = v;
        c->vreg_in[v] = r;
        c->dirty[r] = 1;
        return r;
    }

    int r = getreg(c, i);
    if (c->reg_holds[r] >= 0) c->vreg_in[c->reg_holds[r]] = -1;
    c->reg_holds[r] = v;
    c->vreg_in[v] = r;
    c->dirty[r] = 1;
    return r;
}

/*
 * Writes back every dirty register whose value is live past the end of the
 * block. Allocation is per block, so the next block looks for its values in
 * their frame slots rather than in whichever register happened to hold them.
 */
static void writeback(C *c) {
    for (int r = 0; r < NREGS; r++) {
        int v = c->reg_holds[r];
        if (v >= 0 && c->dirty[r] && bs_get(&c->fn->blocks[c->block].live_out, v))
            bc_emit(c, (Bc){BC_STORE, 0, 0, v, r, 0, 0, 0, 0});
    }
}

static void clear_regs(C *c) {
    for (int r = 0; r < NREGS; r++) {
        c->reg_holds[r] = -1;
        c->dirty[r] = 0;
    }
    for (int v = 0; v < c->nv; v++) c->vreg_in[v] = -1;
}

static void flush_block(C *c) {
    writeback(c);
    clear_regs(c);
}

/* ---- step 3: emission ---------------------------------------------- */

/* Where each label ends up in the bytecode. */
static int *label_pcs(const Func *fn, int *out_max) {
    int max = -1;
    for (int i = 0; i < fn->ir.count; i++)
        if (fn->ir.items[i].dst.kind == AR_LABEL && fn->ir.items[i].dst.id > max)
            max = fn->ir.items[i].dst.id;
    *out_max = max;
    if (max < 0) return NULL;
    int *m = xmalloc((size_t)(max + 1) * sizeof *m);
    for (int i = 0; i <= max; i++) m[i] = -1;
    return m;
}

void codegen_run(Func *fn, OptStats *stats, int is_main) {
    free(fn->bc);
    fn->bc = NULL;
    fn->nbc = fn->bc_cap = 0;

    cfg_liveness(fn);
    if (fuse_compares(fn, stats)) compact_nops(fn);
    /* compact_nops rebuilds the graph, so every analysis built on it has to
     * be redone before anything downstream looks at it. */
    cfg_liveness(fn);
    cfg_dominators(fn);
    cfg_loops(fn);

    fn->nslots = fn->nvregs;

    C ctx = {0};
    C *c = &ctx;
    c->fn = fn;
    c->nv = fn->nvregs > 0 ? fn->nvregs : 1;
    c->nu = compute_nextuse(fn);
    c->vreg_in = xmalloc((size_t)c->nv * sizeof *c->vreg_in);

    int lmax;
    int *lpc = label_pcs(fn, &lmax);

    for (int b = 0; b < fn->nblocks; b++) {
        c->block = b;
        for (int r = 0; r < NREGS; r++) {
            c->reg_holds[r] = -1;
            c->dirty[r] = 0;
        }
        for (int v = 0; v < c->nv; v++) c->vreg_in[v] = -1;
        unpin_all(c);

        if (!fn->blocks[b].reachable) continue;

        for (int i = fn->blocks[b].first; i <= fn->blocks[b].last; i++) {
            const Ir *ir = &fn->ir.items[i];
            unpin_all(c);
            int line = ir->line, col = ir->col;

            switch (ir->op) {
            case IR_NOP: break;

            case IR_LABEL:
                if (lpc && ir->dst.id >= 0 && ir->dst.id <= lmax) lpc[ir->dst.id] = fn->nbc;
                break;

            case IR_CONST: {
                int d = dest_reg(c, ir->dst.id, i);
                double k = ir->a.kind == AR_CONST ? ir->a.k : (double)ir->a.id;
                bc_emit(c, (Bc){BC_LOADK, 0, 0, d, 0, 0, k, line, col});
                break;
            }

            case IR_COPY: {
                if (ir->a.kind == AR_VREG) {
                    int s = in_reg(c, ir->a, i);
                    int d = dest_reg(c, ir->dst.id, i);
                    if (d != s) bc_emit(c, (Bc){BC_MOVE, 0, 0, d, s, 0, 0, line, col});
                } else {
                    int d = dest_reg(c, ir->dst.id, i);
                    double k = ir->a.kind == AR_CONST ? ir->a.k : (double)ir->a.id;
                    bc_emit(c, (Bc){BC_LOADK, 0, 0, d, 0, 0, k, line, col});
                }
                break;
            }

            case IR_BIN: {
                int ra = in_reg(c, ir->a, i);
                int rb = in_reg(c, ir->b, i);
                int d = dest_reg(c, ir->dst.id, i);
                bc_emit(c, (Bc){BC_BIN, ir->bop, 0, d, ra, rb, 0, line, col});
                break;
            }

            case IR_UN: {
                int ra = in_reg(c, ir->a, i);
                int d = dest_reg(c, ir->dst.id, i);
                bc_emit(c, (Bc){BC_UN, ir->bop, 0, d, ra, 0, 0, line, col});
                break;
            }

            case IR_FIELD: {
                int ra = in_reg(c, ir->a, i);
                int d = dest_reg(c, ir->dst.id, i);
                bc_emit(c, (Bc){BC_FIELD, 0, ir->aux, d, ra, 0, 0, line, col});
                break;
            }

            case IR_BUILTIN: {
                int ra = BUILTINS[ir->aux].arity > 0 ? in_reg(c, ir->a, i) : 0;
                int rb = BUILTINS[ir->aux].arity > 1 ? in_reg(c, ir->b, i) : 0;
                int d = dest_reg(c, ir->dst.id, i);
                bc_emit(c, (Bc){BC_BI, 0, ir->aux, d, ra, rb, 0, line, col});
                break;
            }

            case IR_GOTO:
                flush_block(c);
                bc_jump(c, BC_JMP, ir->dst.id, 0, 0, 0, line, col);
                break;

            case IR_IFTRUE:
            case IR_IFFALSE: {
                int ra = in_reg(c, ir->a, i);
                /* Values are written back before the branch, since either
                 * path expects to find them in their slots. The condition
                 * stays in its register, which a store does not disturb. */
                writeback(c);
                bc_jump(c, ir->op == IR_IFTRUE ? BC_JT : BC_JF, ir->dst.id, 0, ra, 0, line, col);
                clear_regs(c);
                break;
            }

            case IR_IFREL: {
                int ra = in_reg(c, ir->a, i);
                int rb = in_reg(c, ir->b, i);
                writeback(c);
                bc_jump(c, BC_JREL, ir->dst.id, ir->bop, ra, rb, line, col);
                clear_regs(c);
                break;
            }

            case IR_PARAM: {
                int ra = in_reg(c, ir->a, i);
                bc_emit(c, (Bc){BC_ARG, 0, 0, c->argn++, ra, 0, 0, line, col});
                break;
            }

            case IR_ENTER:
                bc_emit(c, (Bc){BC_ENTER, 0, 0, ir->a.id, 0, 0, 0, line, col});
                c->argn = 0;
                break;

            case IR_CALL:
                /* The callee has its own registers but shares the simulated
                 * world, so everything live is written back first. */
                flush_block(c);
                bc_emit(c, (Bc){BC_CALL, 0, 0, ir->a.id, ir->aux, 0, 0, line, col});
                c->argn = 0;
                break;

            case IR_CLEAR: {
                int rf = in_reg(c, ir->a, i);
                int rv = in_reg(c, ir->b, i);
                /* `a` carries analysis.c's "direction already checked" flag. */
                bc_emit(c, (Bc){BC_CLEAR, 0, ir->aux, ir->bop, rf, rv, 0, line, col});
                break;
            }

            case IR_ADVANCE: {
                int ra = in_reg(c, ir->a, i);
                bc_emit(c, (Bc){BC_ADV, 0, 0, 0, ra, 0, 0, line, col});
                break;
            }

            case IR_ASSERT: {
                int ra = in_reg(c, ir->a, i);
                bc_emit(c, (Bc){BC_ASSERT, 0, ir->b.kind == AR_STR ? ir->b.id : -1, 0, ra, 0, 0,
                                line, col});
                break;
            }

            /* `c` carries the end-of-print-statement marker set in ir.c. */
            case IR_PRINT: {
                int ra = in_reg(c, ir->a, i);
                bc_emit(c, (Bc){BC_PRINT, 0, ir->aux, 0, ra, ir->bop, 0, line, col});
                break;
            }

            case IR_PRINTSTR:
                bc_emit(c, (Bc){BC_PRINTS, 0, ir->a.id, 0, 0, ir->bop, 0, line, col});
                break;

            case IR_RETURN:
                flush_block(c);
                bc_emit(c, (Bc){is_main ? BC_HALT : BC_RET, 0, 0, 0, 0, 0, 0, line, col});
                break;
            }
        }

        /* A block that falls through to the next must leave its live values
         * where the next block expects them: in their frame slots. */
        const Ir *last = &fn->ir.items[fn->blocks[b].last];
        if (!ir_is_jump(last)) flush_block(c);
    }

    /* Resolve jump targets. */
    for (int i = 0; i < c->nfix; i++) {
        int label = c->fix[i].label;
        int pc = (lpc && label >= 0 && label <= lmax) ? lpc[label] : -1;
        fn->bc[c->fix[i].at].a = pc >= 0 ? pc : fn->nbc;
    }

    free(lpc);
    free(c->fix);
    free(c->vreg_in);
    free(c->nu);
}

/* ---- bytecode peephole (SPEC 11.4) -------------------------------- */

static void peephole(Func *fn, OptStats *stats) {
    if (fn->nbc == 0) return;
    char *dead = xcalloc((size_t)fn->nbc, 1);
    int n = 0;

    for (int i = 0; i < fn->nbc; i++) {
        Bc *b = &fn->bc[i];

        /* A move into the register the value is already in. */
        if (b->op == BC_MOVE && b->a == b->b) {
            dead[i] = 1;
            n++;
            continue;
        }

        /* A store immediately followed by a load of the same slot back into
         * the same register: the value is already there. */
        if (b->op == BC_STORE && i + 1 < fn->nbc) {
            Bc *nx = &fn->bc[i + 1];
            if (nx->op == BC_LOAD && nx->b == b->a && nx->a == b->b) {
                dead[i + 1] = 1;
                n++;
            }
        }

        /* A jump to the next instruction. */
        if (b->op == BC_JMP && b->a == i + 1) {
            dead[i] = 1;
            n++;
            continue;
        }

        /* A conditional branch over an unconditional jump: invert the
         * condition, take the jump's target, and drop the jump. This is
         * the shape `if c goto L1; goto L2; L1:` that jumping code for
         * conditions naturally produces. */
        if ((b->op == BC_JT || b->op == BC_JF || b->op == BC_JREL) && b->a == i + 2 &&
            i + 1 < fn->nbc && fn->bc[i + 1].op == BC_JMP && !dead[i + 1]) {
            if (b->op == BC_JT)
                b->op = BC_JF;
            else if (b->op == BC_JF)
                b->op = BC_JT;
            else
                b->bop = invert_rel(b->bop);
            b->a = fn->bc[i + 1].a;
            dead[i + 1] = 1;
            n++;
            continue;
        }

        /* A jump to a jump. */
        if ((b->op == BC_JMP || b->op == BC_JT || b->op == BC_JF || b->op == BC_JREL) &&
            b->a >= 0 && b->a < fn->nbc) {
            for (int hops = 0; hops < 16; hops++) {
                int t = b->a;
                if (t < 0 || t >= fn->nbc || fn->bc[t].op != BC_JMP) break;
                if (fn->bc[t].a == t) break;
                b->a = fn->bc[t].a;
                n++;
            }
        }
    }

    /* Compact, remapping every jump target through the new positions. */
    int *map = xmalloc((size_t)(fn->nbc + 1) * sizeof *map);
    int out = 0;
    for (int i = 0; i < fn->nbc; i++) {
        map[i] = out;
        if (!dead[i]) out++;
    }
    map[fn->nbc] = out;

    Bc *nb = xmalloc((size_t)(out > 0 ? out : 1) * sizeof *nb);
    int j = 0;
    for (int i = 0; i < fn->nbc; i++) {
        if (dead[i]) continue;
        nb[j] = fn->bc[i];
        switch (nb[j].op) {
        case BC_JMP:
        case BC_JT:
        case BC_JF:
        case BC_JREL:
            if (nb[j].a >= 0 && nb[j].a <= fn->nbc) nb[j].a = map[nb[j].a];
            break;
        default: break;
        }
        j++;
    }
    free(fn->bc);
    free(dead);
    free(map);
    fn->bc = nb;
    fn->nbc = out;
    fn->bc_cap = out;
    if (stats) stats->peephole += n;
}

void codegen_run_all(FuncList *fns, OptStats *stats) {
    for (int i = 0; i < fns->count; i++) {
        codegen_run(&fns->items[i], stats, i == 0);
        peephole(&fns->items[i], stats);
    }
}

/* ---- printing ------------------------------------------------------ */

void bc_print(FILE *f, const Program *p, const Func *fn) {
    char buf[64];
    fprintf(f, "bytecode for %s: %d instruction%s, %d frame slot%s, %d registers\n",
            str_cstr(fn->name, buf, sizeof buf), fn->nbc, fn->nbc == 1 ? "" : "s", fn->nslots,
            fn->nslots == 1 ? "" : "s", NREGS);

    for (int i = 0; i < fn->nbc; i++) {
        const Bc *b = &fn->bc[i];
        fprintf(f, "%4d  %-7s", i, bc_op_name(b->op));
        switch (b->op) {
        case BC_LOADK: fprintf(f, "r%d, %g", b->a, b->k); break;
        case BC_MOVE: fprintf(f, "r%d, r%d", b->a, b->b); break;
        case BC_BIN: fprintf(f, "r%d, r%d %s r%d", b->a, b->b, op_symbol(b->bop), b->c); break;
        case BC_UN: fprintf(f, "r%d, %s r%d", b->a, op_symbol(b->bop), b->b); break;
        case BC_LOAD:
            fprintf(f, "r%d, slot %d  (", b->a, b->b);
            ir_print_vreg(f, fn, b->b);
            fputc(')', f);
            break;
        case BC_STORE:
            fprintf(f, "slot %d, r%d  (", b->a, b->b);
            ir_print_vreg(f, fn, b->a);
            fputc(')', f);
            break;
        case BC_JMP: fprintf(f, "%d", b->a); break;
        case BC_JT:
        case BC_JF: fprintf(f, "r%d, %d", b->b, b->a); break;
        case BC_JREL: fprintf(f, "r%d %s r%d, %d", b->b, op_symbol(b->bop), b->c, b->a); break;
        case BC_FIELD: fprintf(f, "r%d, r%d.%s", b->a, b->b, field_name((Field)b->aux)); break;
        case BC_BI:
            fprintf(f, "r%d, %s(r%d", b->a, BUILTINS[b->aux].name, b->b);
            if (BUILTINS[b->aux].arity > 1) fprintf(f, ", r%d", b->c);
            fputc(')', f);
            break;
        case BC_ENTER:
            if (p && b->a >= 0 && b->a < p->nflights)
                fprintf(f, "%s", str_cstr(p->flights[b->a].name, buf, sizeof buf));
            else
                fprintf(f, "flight %d", b->a);
            break;
        case BC_CLEAR: fprintf(f, "r%d, %s, r%d", b->b, CLEARS[b->aux].name, b->c); break;
        case BC_ADV: fprintf(f, "r%d", b->b); break;
        case BC_ASSERT:
            fprintf(f, "r%d", b->b);
            if (p && b->aux >= 0 && b->aux < p->nstrings) fprintf(f, ", \"%s\"", p->strings[b->aux]);
            break;
        case BC_PRINT: fprintf(f, "r%d", b->b); break;
        case BC_PRINTS:
            if (p && b->aux >= 0 && b->aux < p->nstrings) fprintf(f, "\"%s\"", p->strings[b->aux]);
            break;
        case BC_ARG: fprintf(f, "%d, r%d", b->a, b->b); break;
        case BC_CALL: fprintf(f, "p%d, %d args", b->a, b->b); break;
        case BC_RET:
        case BC_HALT: break;
        }
        fputc('\n', f);
    }
}

void bc_print_all(FILE *f, const Program *p, const FuncList *fns) {
    for (int i = 0; i < fns->count; i++) {
        if (i) fputc('\n', f);
        bc_print(f, p, &fns->items[i]);
    }
}
