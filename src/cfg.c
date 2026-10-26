/*
 * Basic blocks, the control-flow graph, and the dataflow analyses built on
 * it (SPEC section 9).
 *
 * Everything the optimiser and the register allocator need to know about
 * where control can go is computed here:
 *
 *   cfg_build       partition into basic blocks and connect them
 *   cfg_liveness    backward dataflow: which values are still needed
 *   cfg_dominators  which blocks must execute before a given block
 *   cfg_loops       back edges, loop headers and nesting depth
 *
 * The dataflow equations are solved by iterating to a fixed point in block
 * order. A reverse-postorder sweep would converge in fewer rounds, but
 * scenarios are small and index order is easier to trace by hand.
 */
#include "aerodsl.h"

/* ---- instruction classification ------------------------------------ */

int ir_is_jump(const Ir *ir) {
    switch (ir->op) {
    case IR_GOTO:
    case IR_IFTRUE:
    case IR_IFFALSE:
    case IR_IFREL:
    case IR_RETURN: return 1;
    default: return 0;
    }
}

int ir_writes(const Ir *ir) {
    switch (ir->op) {
    case IR_CONST:
    case IR_COPY:
    case IR_BIN:
    case IR_UN:
    case IR_FIELD:
    case IR_BUILTIN: return ir->dst.kind == AR_VREG ? ir->dst.id : -1;
    default: return -1;
    }
}

int ir_reads(const Ir *ir, int reads[2]) {
    int n = 0;
    Arg cand[2] = {{AR_NONE, 0, 0}, {AR_NONE, 0, 0}};
    switch (ir->op) {
    case IR_COPY:
    case IR_UN:
    case IR_FIELD:
    case IR_IFTRUE:
    case IR_IFFALSE:
    case IR_ADVANCE:
    case IR_ASSERT:
    case IR_PRINT:
    case IR_PARAM: cand[0] = ir->a; break;
    case IR_BIN:
    case IR_IFREL:
    case IR_CLEAR:
        cand[0] = ir->a;
        cand[1] = ir->b;
        break;
    case IR_BUILTIN:
        if (BUILTINS[ir->aux].arity > 0) cand[0] = ir->a;
        if (BUILTINS[ir->aux].arity > 1) cand[1] = ir->b;
        break;
    /* IR_ENTER's operand is an immediate flight index; its three values
     * arrive through the preceding IR_PARAM instructions. A call does not
     * touch the caller's registers, because the callee has its own frame. */
    default: break;
    }
    for (int i = 0; i < 2; i++)
        if (cand[i].kind == AR_VREG) reads[n++] = cand[i].id;
    return n;
}

/* Instructions that must never be deleted, however dead their result. */
int ir_has_side_effect(const Ir *ir) {
    switch (ir->op) {
    case IR_ENTER:
    case IR_CLEAR:
    case IR_ADVANCE:
    case IR_ASSERT:
    case IR_PRINT:
    case IR_PRINTSTR:
    case IR_PARAM:
    case IR_CALL:
    case IR_RETURN:
    case IR_GOTO:
    case IR_IFTRUE:
    case IR_IFFALSE:
    case IR_IFREL:
    case IR_LABEL: return 1;
    default: return 0;
    }
}

/*
 * A barrier can change the simulated world, so a read of that world must
 * never be moved across it (SPEC 10.6). This is the one domain-specific rule
 * the optimiser obeys, and without it a loop waiting for an aircraft to
 * climb would have its altitude read hoisted out and would never terminate.
 */
int ir_is_barrier(const Ir *ir) {
    switch (ir->op) {
    case IR_ADVANCE:
    case IR_CLEAR:
    case IR_ENTER:
    case IR_CALL: return 1;
    default: return 0;
    }
}

/* Reads of the simulated world, which barriers invalidate. */
int ir_is_state_dependent(const Ir *ir) {
    if (ir->op == IR_FIELD) return 1;
    if (ir->op == IR_BUILTIN) {
        switch ((Builtin)ir->aux) {
        case BI_VSEP:
        case BI_HSEP:
        case BI_DIST:
        case BI_BEARING:
        case BI_NOW: return 1;
        default: return 0;
        }
    }
    return 0;
}

/* ---- block construction -------------------------------------------- */

int cfg_block_of(const Func *fn, int ir_index) {
    for (int b = 0; b < fn->nblocks; b++)
        if (ir_index >= fn->blocks[b].first && ir_index <= fn->blocks[b].last) return b;
    return -1;
}

/* Maps a label id to the instruction index of its IR_LABEL. */
static int *label_map(const Func *fn, int *out_max) {
    int max = -1;
    for (int i = 0; i < fn->ir.count; i++) {
        const Ir *ir = &fn->ir.items[i];
        if (ir->op == IR_LABEL && ir->dst.id > max) max = ir->dst.id;
        if (ir->dst.kind == AR_LABEL && ir->dst.id > max) max = ir->dst.id;
    }
    *out_max = max;
    if (max < 0) return NULL;
    int *m = xmalloc((size_t)(max + 1) * sizeof *m);
    for (int i = 0; i <= max; i++) m[i] = -1;
    for (int i = 0; i < fn->ir.count; i++)
        if (fn->ir.items[i].op == IR_LABEL) m[fn->ir.items[i].dst.id] = i;
    return m;
}

void cfg_build(Func *fn) {
    cfg_free(fn);
    if (fn->ir.count == 0) return;

    int lmax;
    int *lmap = label_map(fn, &lmax);

    /* Leaders: the first instruction, every label, and every instruction
     * that follows a jump. */
    char *leader = xcalloc((size_t)fn->ir.count, 1);
    leader[0] = 1;
    for (int i = 0; i < fn->ir.count; i++) {
        if (fn->ir.items[i].op == IR_LABEL) leader[i] = 1;
        if (ir_is_jump(&fn->ir.items[i]) && i + 1 < fn->ir.count) leader[i + 1] = 1;
    }

    int n = 0;
    for (int i = 0; i < fn->ir.count; i++)
        if (leader[i]) n++;
    fn->blocks = xcalloc((size_t)n, sizeof *fn->blocks);
    fn->nblocks = n;

    int b = -1;
    for (int i = 0; i < fn->ir.count; i++) {
        if (leader[i]) {
            b++;
            fn->blocks[b].first = i;
            fn->blocks[b].idom = -1;
        }
        fn->blocks[b].last = i;
    }
    free(leader);

    /* Edges. */
    for (int i = 0; i < fn->nblocks; i++) {
        const Ir *last = &fn->ir.items[fn->blocks[i].last];
        int fallthrough = 1, target = -1;

        if (last->dst.kind == AR_LABEL && last->op != IR_LABEL) {
            if (last->dst.id >= 0 && lmap && last->dst.id <= lmax && lmap[last->dst.id] >= 0)
                target = cfg_block_of(fn, lmap[last->dst.id]);
        }
        switch (last->op) {
        case IR_GOTO: fallthrough = 0; break;
        case IR_RETURN:
            fallthrough = 0;
            target = -1;
            break;
        default: break;
        }

        if (target >= 0) {
            intlist_push(&fn->blocks[i].succ, target);
            intlist_push(&fn->blocks[target].pred, i);
        }
        if (fallthrough && i + 1 < fn->nblocks && target != i + 1) {
            intlist_push(&fn->blocks[i].succ, i + 1);
            intlist_push(&fn->blocks[i + 1].pred, i);
        }
    }
    free(lmap);

    /* Reachability from the entry block. */
    IntList work = {0};
    fn->blocks[0].reachable = 1;
    intlist_push(&work, 0);
    while (work.count) {
        int cur = work.items[--work.count];
        for (int i = 0; i < fn->blocks[cur].succ.count; i++) {
            int s = fn->blocks[cur].succ.items[i];
            if (!fn->blocks[s].reachable) {
                fn->blocks[s].reachable = 1;
                intlist_push(&work, s);
            }
        }
    }
    intlist_free(&work);
}

void cfg_free(Func *fn) {
    for (int i = 0; i < fn->nblocks; i++) {
        Block *b = &fn->blocks[i];
        intlist_free(&b->succ);
        intlist_free(&b->pred);
        bs_free(&b->use);
        bs_free(&b->def);
        bs_free(&b->live_in);
        bs_free(&b->live_out);
        bs_free(&b->dom);
    }
    free(fn->blocks);
    fn->blocks = NULL;
    fn->nblocks = 0;
}

/* ---- liveness ------------------------------------------------------ */

void cfg_liveness(Func *fn) {
    int nv = fn->nvregs > 0 ? fn->nvregs : 1;

    for (int i = 0; i < fn->nblocks; i++) {
        Block *b = &fn->blocks[i];
        bs_free(&b->use);
        bs_free(&b->def);
        bs_free(&b->live_in);
        bs_free(&b->live_out);
        bs_init(&b->use, nv);
        bs_init(&b->def, nv);
        bs_init(&b->live_in, nv);
        bs_init(&b->live_out, nv);

        /* USE collects uses that are "upward exposed": read in the block
         * before any assignment to the same register within it. */
        BitSet killed;
        bs_init(&killed, nv);
        for (int j = b->first; j <= b->last; j++) {
            const Ir *ir = &fn->ir.items[j];
            int r[2], nr = ir_reads(ir, r);
            for (int k = 0; k < nr; k++)
                if (!bs_get(&killed, r[k])) bs_set(&b->use, r[k]);
            int w = ir_writes(ir);
            if (w >= 0) {
                bs_set(&b->def, w);
                bs_set(&killed, w);
            }
        }
        bs_free(&killed);
    }

    /* LIVEout(B) = union of LIVEin(S) over successors S
     * LIVEin(B)  = USE(B) union (LIVEout(B) minus DEF(B))            */
    BitSet tmp;
    bs_init(&tmp, nv);
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = fn->nblocks - 1; i >= 0; i--) {
            Block *b = &fn->blocks[i];
            bs_clear(&b->live_out);
            for (int j = 0; j < b->succ.count; j++)
                bs_union(&b->live_out, &fn->blocks[b->succ.items[j]].live_in);

            bs_copy(&tmp, &b->live_out);
            for (int v = 0; v < nv; v++)
                if (bs_get(&b->def, v)) bs_unset(&tmp, v);
            bs_union(&tmp, &b->use);

            if (!bs_eq(&tmp, &b->live_in)) {
                bs_copy(&b->live_in, &tmp);
                changed = 1;
            }
        }
    }
    bs_free(&tmp);
}

/* ---- dominators ---------------------------------------------------- */

void cfg_dominators(Func *fn) {
    if (fn->nblocks == 0) return;
    int n = fn->nblocks;

    for (int i = 0; i < n; i++) {
        bs_free(&fn->blocks[i].dom);
        bs_init(&fn->blocks[i].dom, n);
        if (i == 0)
            bs_set(&fn->blocks[i].dom, 0);
        else
            bs_fill(&fn->blocks[i].dom);
        fn->blocks[i].idom = -1;
    }

    /* DOM(n) = {n} union intersection of DOM(p) over predecessors p. */
    BitSet tmp;
    bs_init(&tmp, n);
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 1; i < n; i++) {
            Block *b = &fn->blocks[i];
            if (!b->reachable) continue;

            int first = 1;
            bs_clear(&tmp);
            for (int j = 0; j < b->pred.count; j++) {
                int p = b->pred.items[j];
                if (!fn->blocks[p].reachable) continue;
                if (first) {
                    bs_copy(&tmp, &fn->blocks[p].dom);
                    first = 0;
                } else {
                    bs_intersect(&tmp, &fn->blocks[p].dom);
                }
            }
            if (first) bs_clear(&tmp); /* no reachable predecessor */
            bs_set(&tmp, i);

            if (!bs_eq(&tmp, &b->dom)) {
                bs_copy(&b->dom, &tmp);
                changed = 1;
            }
        }
    }
    bs_free(&tmp);

    /* The immediate dominator is the strict dominator that every other
     * strict dominator also dominates -- equivalently, the one with the
     * largest dominator set of its own. */
    for (int i = 1; i < n; i++) {
        Block *b = &fn->blocks[i];
        if (!b->reachable) continue;
        int best = -1, best_size = -1;
        for (int d = 0; d < n; d++) {
            if (d == i || !bs_get(&b->dom, d)) continue;
            int size = bs_count(&fn->blocks[d].dom);
            if (size > best_size) {
                best_size = size;
                best = d;
            }
        }
        b->idom = best;
    }
}

/* ---- loops --------------------------------------------------------- */

int cfg_back_edges(const Func *fn, int *tails, int *headers, int max) {
    int n = 0;
    for (int i = 0; i < fn->nblocks; i++) {
        if (!fn->blocks[i].reachable) continue;
        for (int j = 0; j < fn->blocks[i].succ.count; j++) {
            int h = fn->blocks[i].succ.items[j];
            if (!bs_get(&fn->blocks[i].dom, h)) continue; /* not a back edge */
            if (n < max) {
                tails[n] = i;
                headers[n] = h;
            }
            n++;
        }
    }
    return n < max ? n : max;
}

void cfg_natural_loop(const Func *fn, int header, int tail, BitSet *body) {
    bs_clear(body);
    bs_set(body, header);
    if (tail == header) return;

    IntList work = {0};
    bs_set(body, tail);
    intlist_push(&work, tail);
    while (work.count) {
        int cur = work.items[--work.count];
        for (int i = 0; i < fn->blocks[cur].pred.count; i++) {
            int p = fn->blocks[cur].pred.items[i];
            if (bs_get(body, p)) continue;
            bs_set(body, p);
            intlist_push(&work, p);
        }
    }
    intlist_free(&work);
}

void cfg_loops(Func *fn) {
    for (int i = 0; i < fn->nblocks; i++) {
        fn->blocks[i].loop_header = 0;
        fn->blocks[i].depth = 0;
    }

    int tails[256], headers[256];
    int n = cfg_back_edges(fn, tails, headers, 256);

    BitSet body;
    bs_init(&body, fn->nblocks > 0 ? fn->nblocks : 1);
    for (int i = 0; i < n; i++) {
        fn->blocks[headers[i]].loop_header = 1;
        cfg_natural_loop(fn, headers[i], tails[i], &body);
        for (int b = 0; b < fn->nblocks; b++)
            if (bs_get(&body, b)) fn->blocks[b].depth++;
    }
    bs_free(&body);
}

/* ---- printing ------------------------------------------------------ */

void cfg_print(FILE *f, const Func *fn) {
    char buf[64];
    fprintf(f, "control-flow graph of %s: %d block%s\n", str_cstr(fn->name, buf, sizeof buf),
            fn->nblocks, fn->nblocks == 1 ? "" : "s");

    for (int i = 0; i < fn->nblocks; i++) {
        const Block *b = &fn->blocks[i];
        fprintf(f, "  B%-3d instructions %d..%d", i, b->first, b->last);
        if (!b->reachable) fputs("  [unreachable]", f);
        if (b->loop_header) fputs("  [loop header]", f);
        if (b->depth) fprintf(f, "  [loop depth %d]", b->depth);
        fputc('\n', f);

        fputs("       succ:", f);
        if (!b->succ.count) fputs(" none", f);
        for (int j = 0; j < b->succ.count; j++) fprintf(f, " B%d", b->succ.items[j]);
        fputs("   pred:", f);
        if (!b->pred.count) fputs(" none", f);
        for (int j = 0; j < b->pred.count; j++) fprintf(f, " B%d", b->pred.items[j]);
        fputc('\n', f);

        if (b->idom >= 0) fprintf(f, "       idom: B%d\n", b->idom);
        if (b->dom.nbits) {
            fputs("       dominated by:", f);
            for (int d = 0; d < fn->nblocks; d++)
                if (bs_get(&b->dom, d)) fprintf(f, " B%d", d);
            fputc('\n', f);
        }
        if (b->live_in.nbits) {
            fputs("       live in:", f);
            int any = 0;
            for (int v = 0; v < fn->nvregs; v++)
                if (bs_get(&b->live_in, v)) {
                    fputc(' ', f);
                    ir_print_vreg(f, fn, v);
                    any = 1;
                }
            if (!any) fputs(" none", f);
            fputs("   live out:", f);
            any = 0;
            for (int v = 0; v < fn->nvregs; v++)
                if (bs_get(&b->live_out, v)) {
                    fputc(' ', f);
                    ir_print_vreg(f, fn, v);
                    any = 1;
                }
            if (!any) fputs(" none", f);
            fputc('\n', f);
        }
    }
}
