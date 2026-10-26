/*
 * AeroDSL optimiser (SPEC section 10).
 *
 * Passes, run in this order and then repeated until nothing changes,
 * because each one creates work for the others: propagation produces
 * constant operands, folding turns those into constants, and the
 * instructions that computed them become dead.
 *
 *   fold      constant folding
 *   constprop global constant propagation (forward dataflow, lattice)
 *   dag       per-block DAG: common subexpressions, copy propagation,
 *             algebraic simplification
 *   dce       dead-code elimination driven by liveness
 *   licm      loop-invariant code motion
 *   jumps     jump threading and unreachable-block removal
 *   compact   remove the nops and unreferenced labels the above leave
 *
 * The one domain-specific rule, and the one without which all of this would
 * be wrong: `clear`, `advance`, `enter` and `call` are *barriers*, because
 * they can change the simulated world. A read of that world -- a flight
 * field, or one of the state-dependent built-ins -- is never moved across a
 * barrier, never treated as a common subexpression across one, and never
 * hoisted out of a loop containing one. Without it, `while AI202.alt <
 * FL300 do advance 10 end` would have its altitude read hoisted out of the
 * loop and would spin forever.
 *
 * Soundness note on the algebraic identities below: x+0, x-0, x*1 and x/1
 * become copies. These are exact for finite values. Non-finite values cannot
 * occur: a constant zero divisor is rejected by the semantic analyser, a
 * runtime zero divisor is a runtime error, and the VM rejects any arithmetic
 * result that is not finite. x*0 and x-x are deliberately *not* simplified,
 * since those would be the identities that need a finiteness assumption at
 * the point of rewriting rather than at the point of execution.
 */
#include <math.h>

#include "aerodsl.h"

/* ---- constant arithmetic ------------------------------------------- */

/* Evaluates op on two constants. Returns 0 if the result is not a usable
 * constant, in which case the instruction is left alone. */
static int fold_bin(BinOp op, double a, double b, double *out) {
    switch (op) {
    case OP_ADD: *out = a + b; break;
    case OP_SUB: *out = a - b; break;
    case OP_MUL: *out = a * b; break;
    case OP_DIV:
        if (b == 0) return 0; /* left for the runtime error */
        *out = a / b;
        break;
    case OP_MOD:
        if (b == 0) return 0;
        *out = fmod(a, b);
        break;
    case OP_LT: *out = a < b; break;
    case OP_LE: *out = a <= b; break;
    case OP_GT: *out = a > b; break;
    case OP_GE: *out = a >= b; break;
    case OP_EQ: *out = a == b; break;
    case OP_NE: *out = a != b; break;
    default: return 0;
    }
    return isfinite(*out);
}

static int fold_un(BinOp op, double a, double *out) {
    if (op == OP_NEG)
        *out = -a;
    else if (op == OP_NOT)
        *out = a == 0 ? 1 : 0;
    else
        return 0;
    return isfinite(*out);
}

static Arg mk_const(double k) { return (Arg){AR_CONST, 0, k}; }
static Arg a_none(void) { return (Arg){AR_NONE, 0, 0}; }

/* An immediate: a value already known at compile time. A flight or waypoint
 * reference counts, which is what lets the analysis pass see which aircraft
 * a clearance through a parameter actually names. */
static int is_imm(Arg a) {
    return a.kind == AR_CONST || a.kind == AR_FLIGHT || a.kind == AR_FIX;
}

static void make_nop(Ir *ir) {
    int line = ir->line, col = ir->col;
    memset(ir, 0, sizeof *ir);
    ir->op = IR_NOP;
    ir->line = line;
    ir->col = col;
}

/* ---- 1. constant folding ------------------------------------------- */

static int pass_fold(Func *fn) {
    int n = 0;
    for (int i = 0; i < fn->ir.count; i++) {
        Ir *ir = &fn->ir.items[i];
        double r;
        if (ir->op == IR_BIN && ir->a.kind == AR_CONST && ir->b.kind == AR_CONST) {
            if (!fold_bin(ir->bop, ir->a.k, ir->b.k, &r)) continue;
            ir->op = IR_CONST;
            ir->a = mk_const(r);
            ir->b = a_none();
            ir->bop = 0;
            n++;
        } else if (ir->op == IR_UN && ir->a.kind == AR_CONST) {
            if (!fold_un(ir->bop, ir->a.k, &r)) continue;
            ir->op = IR_CONST;
            ir->a = mk_const(r);
            ir->bop = 0;
            n++;
        } else if (ir->op == IR_BUILTIN) {
            /* Only the pure built-ins fold; the rest read the simulation. */
            if (ir_is_state_dependent(ir)) continue;
            Builtin fn_id = (Builtin)ir->aux;
            if (ir->a.kind != AR_CONST) continue;
            if (BUILTINS[fn_id].arity == 2 && ir->b.kind != AR_CONST) continue;
            double v;
            switch (fn_id) {
            case BI_ABS: v = fabs(ir->a.k); break;
            case BI_MIN: v = ir->a.k < ir->b.k ? ir->a.k : ir->b.k; break;
            case BI_MAX: v = ir->a.k > ir->b.k ? ir->a.k : ir->b.k; break;
            default: continue;
            }
            if (!isfinite(v)) continue;
            ir->op = IR_CONST;
            ir->a = mk_const(v);
            ir->b = a_none();
            ir->aux = 0;
            n++;
        }
    }
    return n;
}

/* ---- 2. global constant propagation -------------------------------- */

enum { L_UNDEF, L_CONST, L_NAC };

typedef struct {
    int state;
    Arg val;
} Lat;

static void lat_join(Lat *dst, const Lat *src) {
    if (src->state == L_UNDEF) return;
    if (dst->state == L_UNDEF) {
        *dst = *src;
        return;
    }
    if (dst->state == L_CONST && src->state == L_CONST && ir_arg_eq(dst->val, src->val)) return;
    dst->state = L_NAC;
}

/* Applies one instruction to the lattice state. */
static void lat_transfer(const Ir *ir, Lat *st) {
    int w = ir_writes(ir);
    if (w < 0) return;

    switch (ir->op) {
    case IR_CONST:
        if (is_imm(ir->a)) {
            st[w].state = L_CONST;
            st[w].val = ir->a;
            return;
        }
        break;
    case IR_COPY:
        if (is_imm(ir->a)) {
            st[w].state = L_CONST;
            st[w].val = ir->a;
            return;
        }
        if (ir->a.kind == AR_VREG) {
            st[w] = st[ir->a.id];
            return;
        }
        break;
    case IR_BIN: {
        Arg a = ir->a, b = ir->b;
        if (a.kind == AR_VREG && st[a.id].state == L_CONST) a = st[a.id].val;
        if (b.kind == AR_VREG && st[b.id].state == L_CONST) b = st[b.id].val;
        double r;
        if (a.kind == AR_CONST && b.kind == AR_CONST && fold_bin(ir->bop, a.k, b.k, &r)) {
            st[w].state = L_CONST;
            st[w].val = mk_const(r);
            return;
        }
        break;
    }
    case IR_UN: {
        Arg a = ir->a;
        if (a.kind == AR_VREG && st[a.id].state == L_CONST) a = st[a.id].val;
        double r;
        if (a.kind == AR_CONST && fold_un(ir->bop, a.k, &r)) {
            st[w].state = L_CONST;
            st[w].val = mk_const(r);
            return;
        }
        break;
    }
    default: break;
    }
    st[w].state = L_NAC;
    st[w].val = a_none();
}

static int pass_constprop(Func *fn) {
    if (fn->nblocks == 0 || fn->nvregs == 0) return 0;
    int nv = fn->nvregs, nb = fn->nblocks;

    Lat *in = xcalloc((size_t)nb * nv, sizeof *in);
    Lat *cur = xcalloc((size_t)nv, sizeof *cur);

    /* Parameters arrive from the caller, so nothing is known about them. */
    for (int v = 0; v < fn->nparams && v < nv; v++) in[v].state = L_NAC;

    int changed = 1, rounds = 0;
    while (changed && rounds++ < 1000) {
        changed = 0;
        for (int b = 0; b < nb; b++) {
            if (!fn->blocks[b].reachable) continue;

            /* Meet over the predecessors' exit states. */
            if (b > 0) {
                Lat *entry = xcalloc((size_t)nv, sizeof *entry);
                for (int j = 0; j < fn->blocks[b].pred.count; j++) {
                    int p = fn->blocks[b].pred.items[j];
                    if (!fn->blocks[p].reachable) continue;
                    memcpy(cur, &in[(size_t)p * nv], (size_t)nv * sizeof *cur);
                    for (int k = fn->blocks[p].first; k <= fn->blocks[p].last; k++)
                        lat_transfer(&fn->ir.items[k], cur);
                    for (int v = 0; v < nv; v++) lat_join(&entry[v], &cur[v]);
                }
                for (int v = 0; v < nv; v++) {
                    Lat *slot = &in[(size_t)b * nv + v];
                    if (slot->state != entry[v].state || !ir_arg_eq(slot->val, entry[v].val)) {
                        *slot = entry[v];
                        changed = 1;
                    }
                }
                free(entry);
            }
        }
    }

    /* Rewrite: replace a register operand by its value where it is known. */
    int n = 0;
    for (int b = 0; b < nb; b++) {
        if (!fn->blocks[b].reachable) continue;
        memcpy(cur, &in[(size_t)b * nv], (size_t)nv * sizeof *cur);
        for (int i = fn->blocks[b].first; i <= fn->blocks[b].last; i++) {
            Ir *ir = &fn->ir.items[i];
            Arg *ops[2] = {&ir->a, &ir->b};
            int r[2], nr = ir_reads(ir, r);
            (void)nr;
            for (int k = 0; k < 2; k++) {
                if (ops[k]->kind != AR_VREG) continue;
                /* Only operands that are actually read may be rewritten. */
                int is_read = 0;
                for (int q = 0; q < nr; q++)
                    if (r[q] == ops[k]->id) is_read = 1;
                if (!is_read) continue;
                Lat *l = &cur[ops[k]->id];
                if (l->state != L_CONST) continue;
                if (ir_arg_eq(*ops[k], l->val)) continue;
                *ops[k] = l->val;
                n++;
            }
            lat_transfer(ir, cur);
        }
    }

    free(in);
    free(cur);
    return n;
}

/* ---- 3. per-block DAG: CSE, copy propagation, algebraic identities -- */

typedef struct {
    IrOp op;
    int bop, aux;
    int a, b;    /* value numbers of the operands */
    int holder;  /* the vreg that currently holds this value */
    int vn;      /* the value number of the result */
    int state;   /* 1 if the computation reads the simulated world */
    int valid;
} Node;

/* An immediate operand and the value number standing for it. */
typedef struct {
    ArgKind kind;
    double k;
    int vn;
} Imm;

typedef struct {
    Func *fn;
    int next_vn;
    int *vn_of;    /* per vreg: the value number it currently holds */
    Arg *copy_of;  /* per vreg: what it was copied from, or AR_NONE */
    Node *nodes;
    int nnodes, nodes_cap;
    Imm *imms; /* immediate value numbers, so two 32000s share one */
    int nimms, imms_cap;
} Dag;

static int vn_new(Dag *d) { return d->next_vn++; }

static int vn_imm(Dag *d, Arg a) {
    double key = a.kind == AR_CONST ? a.k : (double)a.id;
    for (int i = 0; i < d->nimms; i++)
        if (d->imms[i].kind == a.kind && d->imms[i].k == key) return d->imms[i].vn;
    int vn = vn_new(d);
    PUSHN(d->imms, d->nimms, d->imms_cap, ((Imm){a.kind, key, vn}));
    return vn;
}

static int vn_of_arg(Dag *d, Arg a) {
    if (a.kind == AR_VREG) return d->vn_of[a.id];
    return vn_imm(d, a);
}

/* A write to v invalidates v's own copy record and any record naming v. */
static void kill_copies(Dag *d, int v) {
    d->copy_of[v] = a_none();
    for (int i = 0; i < d->fn->nvregs; i++)
        if (d->copy_of[i].kind == AR_VREG && d->copy_of[i].id == v) d->copy_of[i] = a_none();
}

static void kill_holder(Dag *d, int v) {
    for (int i = 0; i < d->nnodes; i++)
        if (d->nodes[i].valid && d->nodes[i].holder == v) d->nodes[i].valid = 0;
}

/* A barrier invalidates every computation that read the simulated world. */
static void kill_state_nodes(Dag *d) {
    for (int i = 0; i < d->nnodes; i++)
        if (d->nodes[i].state) d->nodes[i].valid = 0;
}

static Node *dag_find(Dag *d, IrOp op, int bop, int aux, int a, int b) {
    for (int i = 0; i < d->nnodes; i++) {
        Node *n = &d->nodes[i];
        if (n->valid && n->op == op && n->bop == bop && n->aux == aux && n->a == a && n->b == b)
            return n;
    }
    return NULL;
}

/* Rewrites x+0, x-0, x*1, x/1 and x*2 in place. Returns 1 if it changed. */
static int simplify_algebraic(Ir *ir) {
    if (ir->op != IR_BIN) return 0;
    int ac = ir->a.kind == AR_CONST, bc = ir->b.kind == AR_CONST;

    switch (ir->bop) {
    case OP_ADD:
        if (bc && ir->b.k == 0) {
            ir->op = IR_COPY;
            ir->b = a_none();
            return 1;
        }
        if (ac && ir->a.k == 0) {
            ir->op = IR_COPY;
            ir->a = ir->b;
            ir->b = a_none();
            return 1;
        }
        return 0;
    case OP_SUB:
        if (bc && ir->b.k == 0) {
            ir->op = IR_COPY;
            ir->b = a_none();
            return 1;
        }
        return 0;
    case OP_MUL:
        if (bc && ir->b.k == 1) {
            ir->op = IR_COPY;
            ir->b = a_none();
            return 1;
        }
        if (ac && ir->a.k == 1) {
            ir->op = IR_COPY;
            ir->a = ir->b;
            ir->b = a_none();
            return 1;
        }
        /* Strength reduction: a doubling becomes an addition. */
        if (bc && ir->b.k == 2 && ir->a.kind == AR_VREG) {
            ir->bop = OP_ADD;
            ir->b = ir->a;
            return 1;
        }
        if (ac && ir->a.k == 2 && ir->b.kind == AR_VREG) {
            ir->bop = OP_ADD;
            ir->a = ir->b;
            return 1;
        }
        return 0;
    case OP_DIV:
        if (bc && ir->b.k == 1) {
            ir->op = IR_COPY;
            ir->b = a_none();
            return 1;
        }
        return 0;
    default: return 0;
    }
}

static int pass_dag(Func *fn, OptStats *stats) {
    if (fn->nblocks == 0) return 0;
    int nv = fn->nvregs > 0 ? fn->nvregs : 1;
    int total = 0;

    Dag d = {0};
    d.fn = fn;
    d.vn_of = xcalloc((size_t)nv, sizeof *d.vn_of);
    d.copy_of = xcalloc((size_t)nv, sizeof *d.copy_of);

    for (int b = 0; b < fn->nblocks; b++) {
        if (!fn->blocks[b].reachable) continue;

        /* Each block starts with no knowledge: the analysis is local. */
        d.next_vn = 0;
        d.nnodes = 0;
        d.nimms = 0;
        for (int v = 0; v < nv; v++) {
            d.vn_of[v] = vn_new(&d);
            d.copy_of[v] = a_none();
        }

        for (int i = fn->blocks[b].first; i <= fn->blocks[b].last; i++) {
            Ir *ir = &fn->ir.items[i];

            /* Copy propagation: read through any copy still in force. */
            Arg *ops[2] = {&ir->a, &ir->b};
            int r[2], nr = ir_reads(ir, r);
            for (int k = 0; k < 2; k++) {
                if (ops[k]->kind != AR_VREG) continue;
                int is_read = 0;
                for (int q = 0; q < nr; q++)
                    if (r[q] == ops[k]->id) is_read = 1;
                if (!is_read) continue;
                Arg src = d.copy_of[ops[k]->id];
                if (src.kind == AR_NONE) continue;
                /* Counting a rewrite that changes nothing would keep the
                 * fixed-point loop running forever. */
                if (ir_arg_eq(*ops[k], src)) continue;
                *ops[k] = src;
                if (stats) stats->copies++;
                total++;
            }

            if (ir_is_barrier(ir)) kill_state_nodes(&d);

            if (simplify_algebraic(ir)) {
                if (stats) stats->algebraic++;
                total++;
            }

            int w = ir_writes(ir);
            if (w < 0) continue;

            /* Common subexpression elimination over the block's DAG. */
            if (ir->op == IR_BIN || ir->op == IR_UN || ir->op == IR_FIELD ||
                ir->op == IR_BUILTIN) {
                int va = vn_of_arg(&d, ir->a);
                int vb = ir->b.kind == AR_NONE ? -1 : vn_of_arg(&d, ir->b);
                Node *hit = dag_find(&d, ir->op, ir->bop, ir->aux, va, vb);
                if (hit && hit->holder != w) {
                    int vn = hit->vn;
                    ir->op = IR_COPY;
                    ir->a = (Arg){AR_VREG, hit->holder, 0};
                    ir->b = a_none();
                    ir->bop = 0;
                    ir->aux = 0;
                    kill_copies(&d, w);
                    kill_holder(&d, w);
                    d.vn_of[w] = vn;
                    d.copy_of[w] = ir->a;
                    if (stats) stats->cse++;
                    total++;
                    continue;
                }
                kill_copies(&d, w);
                kill_holder(&d, w);
                int vn = vn_new(&d);
                d.vn_of[w] = vn;
                Node node = {ir->op,  ir->bop, ir->aux, va, vb, w, vn,
                             ir_is_state_dependent(ir), 1};
                PUSHN(d.nodes, d.nnodes, d.nodes_cap, node);
                continue;
            }

            kill_copies(&d, w);
            kill_holder(&d, w);

            if (ir->op == IR_COPY || ir->op == IR_CONST) {
                d.vn_of[w] = vn_of_arg(&d, ir->a);
                /* Record the copy so later reads can use the source. A copy
                 * from a register only helps if that register is not the
                 * destination itself. */
                if (!(ir->a.kind == AR_VREG && ir->a.id == w)) d.copy_of[w] = ir->a;
            } else {
                d.vn_of[w] = vn_new(&d);
            }
        }
    }

    free(d.vn_of);
    free(d.copy_of);
    free(d.nodes);
    free(d.imms);
    return total;
}

/* ---- 4. dead-code elimination -------------------------------------- */

static int pass_dce(Func *fn) {
    if (fn->nblocks == 0) return 0;
    cfg_liveness(fn);
    int nv = fn->nvregs > 0 ? fn->nvregs : 1;
    int n = 0;

    BitSet live;
    bs_init(&live, nv);
    for (int b = 0; b < fn->nblocks; b++) {
        Block *blk = &fn->blocks[b];
        if (!blk->reachable) continue;
        bs_copy(&live, &blk->live_out);

        for (int i = blk->last; i >= blk->first; i--) {
            Ir *ir = &fn->ir.items[i];
            int w = ir_writes(ir);

            if (w >= 0 && !bs_get(&live, w) && !ir_has_side_effect(ir)) {
                make_nop(ir);
                n++;
                continue;
            }
            if (w >= 0) bs_unset(&live, w);
            int r[2], nr = ir_reads(ir, r);
            for (int k = 0; k < nr; k++) bs_set(&live, r[k]);
        }
    }
    bs_free(&live);
    return n;
}

/* ---- 5. loop-invariant code motion --------------------------------- */

/* Can this instruction fail at runtime? Such an instruction must not be
 * hoisted, because the loop body might never have run. Division is the one
 * operation that can fail on ordinary values (a zero divisor), so it stays
 * where it is; see the soundness note at the top of this file. */
static int may_fail(const Ir *ir) {
    if (ir->op != IR_BIN) return 0;
    return ir->bop == OP_DIV || ir->bop == OP_MOD;
}

/*
 * Is w still needed after the loop? If so, hoisting its definition would
 * change the program: a loop whose body never runs would see the hoisted
 * value instead of whatever w held before. The standard condition is that
 * the definition dominates every loop exit; requiring the value to be dead
 * on every exit edge is the stricter and simpler form of it.
 *
 *   let x = 1
 *   while c do x = 5  advance 1 end
 *   print x
 *
 * `x = 5` is loop-invariant by every other test, and hoisting it would print
 * 5 even when the loop never ran.
 */
static int live_outside_loop(const Func *fn, const BitSet *body, int w) {
    for (int b = 0; b < fn->nblocks; b++) {
        if (!bs_get(body, b)) continue;
        for (int j = 0; j < fn->blocks[b].succ.count; j++) {
            int s = fn->blocks[b].succ.items[j];
            if (bs_get(body, s)) continue; /* still inside the loop */
            if (bs_get(&fn->blocks[s].live_in, w)) return 1;
        }
    }
    return 0;
}

static int pass_licm(Func *fn, OptStats *stats) {
    if (fn->nblocks == 0) return 0;
    cfg_dominators(fn);
    cfg_liveness(fn);

    int tails[64], headers[64];
    int nloops = cfg_back_edges(fn, tails, headers, 64);
    if (nloops == 0) return 0;

    int nv = fn->nvregs > 0 ? fn->nvregs : 1;
    BitSet body;
    bs_init(&body, fn->nblocks);

    for (int li = 0; li < nloops; li++) {
        int header = headers[li];
        cfg_natural_loop(fn, header, tails[li], &body);

        /* The hoisted code is placed just before the header's label, so it
         * must be reached only by falling into the loop. If any block
         * outside the loop jumps to the header, or more than one outside
         * block reaches it, that placement would be skipped or duplicated,
         * so the loop is left alone. */
        int outside_preds = 0, jumps_in = 0;
        for (int j = 0; j < fn->blocks[header].pred.count; j++) {
            int p = fn->blocks[header].pred.items[j];
            if (bs_get(&body, p)) continue;
            outside_preds++;
            if (p != header - 1) jumps_in = 1;
            const Ir *last = &fn->ir.items[fn->blocks[p].last];
            if (last->op == IR_GOTO) jumps_in = 1;
        }
        if (outside_preds != 1 || jumps_in) continue;

        /* How many times is each register assigned inside the loop? */
        int *defs = xcalloc((size_t)nv, sizeof *defs);
        int has_barrier = 0;
        for (int b = 0; b < fn->nblocks; b++) {
            if (!bs_get(&body, b)) continue;
            for (int i = fn->blocks[b].first; i <= fn->blocks[b].last; i++) {
                int w = ir_writes(&fn->ir.items[i]);
                if (w >= 0) defs[w]++;
                if (ir_is_barrier(&fn->ir.items[i])) has_barrier = 1;
            }
        }

        /* Iterate to a fixed point: an instruction is invariant when every
         * operand is a constant, or defined outside the loop, or defined by
         * an instruction already found invariant. */
        char *invariant = xcalloc((size_t)fn->ir.count, 1);
        int added = 1;
        while (added) {
            added = 0;
            for (int b = 0; b < fn->nblocks; b++) {
                if (!bs_get(&body, b)) continue;
                for (int i = fn->blocks[b].first; i <= fn->blocks[b].last; i++) {
                    if (invariant[i]) continue;
                    Ir *ir = &fn->ir.items[i];
                    int w = ir_writes(ir);
                    if (w < 0 || ir_has_side_effect(ir) || may_fail(ir)) continue;
                    if (defs[w] != 1) continue;
                    if (ir_is_state_dependent(ir) && has_barrier) continue;
                    if (live_outside_loop(fn, &body, w)) continue;

                    int ok = 1;
                    int r[2], nr = ir_reads(ir, r);
                    for (int k = 0; k < nr && ok; k++) {
                        if (defs[r[k]] == 0) continue; /* defined outside */
                        /* Defined inside: only acceptable if that single
                         * definition is itself invariant. */
                        int found = 0;
                        for (int bb = 0; bb < fn->nblocks && !found; bb++) {
                            if (!bs_get(&body, bb)) continue;
                            for (int jj = fn->blocks[bb].first; jj <= fn->blocks[bb].last; jj++)
                                if (ir_writes(&fn->ir.items[jj]) == r[k]) {
                                    found = invariant[jj];
                                    break;
                                }
                        }
                        if (!found) ok = 0;
                    }
                    if (!ok) continue;
                    invariant[i] = 1;
                    added = 1;
                }
            }
        }

        /* Hoist: build a new instruction list with the invariant
         * instructions moved to just before the loop header's label. */
        int nhoist = 0;
        for (int i = 0; i < fn->ir.count; i++)
            if (invariant[i]) nhoist++;

        if (nhoist > 0) {
            IrList out = {0};
            int hstart = fn->blocks[header].first;
            for (int i = 0; i < fn->ir.count; i++) {
                if (i == hstart)
                    for (int j = 0; j < fn->ir.count; j++)
                        if (invariant[j]) PUSH(out, fn->ir.items[j]);
                if (invariant[i]) continue;
                PUSH(out, fn->ir.items[i]);
            }
            free(fn->ir.items);
            fn->ir = out;
            if (stats) stats->hoisted += nhoist;

            free(defs);
            free(invariant);
            cfg_build(fn);
            cfg_dominators(fn);
            bs_free(&body);
            return nhoist; /* indices moved; start the next round afresh */
        }

        free(defs);
        free(invariant);
    }

    bs_free(&body);
    return 0;
}

/* ---- 6. jump threading and unreachable code ------------------------ */

static int label_index(const Func *fn, int label) {
    for (int i = 0; i < fn->ir.count; i++)
        if (fn->ir.items[i].op == IR_LABEL && fn->ir.items[i].dst.id == label) return i;
    return -1;
}

/* The next instruction that does something, skipping labels and nops. */
static int next_real(const Func *fn, int i) {
    for (int j = i; j < fn->ir.count; j++) {
        IrOp op = fn->ir.items[j].op;
        if (op != IR_LABEL && op != IR_NOP) return j;
    }
    return -1;
}

static int pass_jumps(Func *fn, DiagList *diags, OptStats *stats) {
    int n = 0;

    for (int i = 0; i < fn->ir.count; i++) {
        Ir *ir = &fn->ir.items[i];

        /* A conditional branch on a known value is not a branch. */
        if ((ir->op == IR_IFTRUE || ir->op == IR_IFFALSE) && ir->a.kind == AR_CONST) {
            int taken = ir->op == IR_IFTRUE ? ir->a.k != 0 : ir->a.k == 0;
            if (taken) {
                ir->op = IR_GOTO;
                ir->a = a_none();
            } else {
                make_nop(ir);
            }
            n++;
            continue;
        }

        if (ir->dst.kind != AR_LABEL || ir->op == IR_LABEL) continue;

        /* Jump to a jump: follow the chain. */
        for (int hops = 0; hops < 16; hops++) {
            int at = label_index(fn, ir->dst.id);
            if (at < 0) break;
            int r = next_real(fn, at + 1);
            if (r < 0 || fn->ir.items[r].op != IR_GOTO) break;
            if (fn->ir.items[r].dst.id == ir->dst.id) break; /* self loop */
            ir->dst = fn->ir.items[r].dst;
            n++;
        }

        /* A jump to the next real instruction is not needed. */
        if (ir->op == IR_GOTO) {
            int at = label_index(fn, ir->dst.id);
            if (at >= 0 && next_real(fn, i + 1) == next_real(fn, at + 1)) {
                make_nop(ir);
                n++;
            }
        }
    }

    /* Unreachable blocks. The first instruction with a source position names
     * the construct that can no longer run. */
    cfg_build(fn);
    for (int b = 0; b < fn->nblocks; b++) {
        if (fn->blocks[b].reachable) continue;
        int reported = 0;
        for (int i = fn->blocks[b].first; i <= fn->blocks[b].last; i++) {
            Ir *ir = &fn->ir.items[i];
            if (ir->op == IR_NOP) continue;
            if (!reported && diags && ir->line > 0 && ir_has_side_effect(ir) &&
                ir->op != IR_LABEL && ir->op != IR_GOTO &&
                !diag_has_at(diags, ir->line)) {
                /* Code after a `return` was already reported by sema; this
                 * message is for code the optimiser proved unreachable. */
                diag_add(diags, PH_SEMANTIC, SEV_WARNING, ir->line, ir->col,
                         "this code can never run");
                reported = 1;
            }
            make_nop(ir);
            n++;
        }
        if (stats) stats->blocks_removed++;
    }
    return n;
}

/* ---- 7. compaction ------------------------------------------------- */

/* Drops nops and labels nothing jumps to. Labels are symbolic, so removing
 * instructions cannot invalidate a jump target. */
static int pass_compact(Func *fn) {
    char used[4096] = {0};
    int maxlabel = LEN(used) - 1;
    for (int i = 0; i < fn->ir.count; i++) {
        const Ir *ir = &fn->ir.items[i];
        if (ir->op != IR_LABEL && ir->dst.kind == AR_LABEL && ir->dst.id >= 0 &&
            ir->dst.id <= maxlabel)
            used[ir->dst.id] = 1;
    }

    IrList out = {0};
    int removed = 0;
    for (int i = 0; i < fn->ir.count; i++) {
        const Ir *ir = &fn->ir.items[i];
        if (ir->op == IR_NOP) {
            removed++;
            continue;
        }
        if (ir->op == IR_LABEL && ir->dst.id >= 0 && ir->dst.id <= maxlabel &&
            !used[ir->dst.id]) {
            removed++;
            continue;
        }
        PUSH(out, *ir);
    }
    if (removed) {
        free(fn->ir.items);
        fn->ir = out;
        cfg_build(fn);
    } else {
        free(out.items);
    }
    return removed;
}

/* ---- driver -------------------------------------------------------- */

void opt_run(Func *fn, OptStats *stats, DiagList *diags) {
    cfg_build(fn);

    for (int round = 0; round < 24; round++) {
        int changed = 0;
        int k;

        /* Propagation first: it turns register operands into constants,
         * which is what gives folding something to do. */
        k = pass_constprop(fn);
        if (stats) stats->propagated += k;
        changed += k;

        k = pass_fold(fn);
        if (stats) stats->folded += k;
        changed += k;

        changed += pass_dag(fn, stats);

        k = pass_dce(fn);
        if (stats) stats->dead += k;
        changed += k;

        changed += pass_licm(fn, stats);

        k = pass_jumps(fn, round == 0 ? diags : NULL, stats);
        if (stats) stats->jumps += k;
        changed += k;

        changed += pass_compact(fn);

        if (!changed) break;
    }

    /* Leave the analyses in a consistent state for the later phases. */
    cfg_build(fn);
    cfg_liveness(fn);
    cfg_dominators(fn);
    cfg_loops(fn);
}

void opt_run_all(FuncList *fns, OptStats *stats, DiagList *diags) {
    for (int i = 0; i < fns->count; i++) opt_run(&fns->items[i], stats, diags);
}
