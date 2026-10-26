/*
 * Domain-specific static analysis (SPEC sections 7.2, 7.4 and 10.8).
 *
 * This runs on the *optimised* three-address code, and that is the point of
 * it. Two things are true there that are not true of the AST:
 *
 *   - Constant propagation has already replaced variables by their values,
 *     so `let h = 450  clear AI202 heading h` is as checkable as a literal
 *     450. The range check therefore lives here rather than in the semantic
 *     analyser, which also keeps it from being reported twice.
 *
 *   - Control flow is explicit, so "two clearances with no `advance` between
 *     them" is a scan of one basic block.
 *
 * The clearance consistency checks are deliberately confined to a single
 * basic block. That makes them sound but incomplete: they never report a
 * contradiction that is not one, and they miss contradictions that span a
 * branch. For a warning about something the author may have written on
 * purpose, that is the right way round.
 */
#include "aerodsl.h"

#define MAX_TRACKED_FLIGHTS 256

typedef struct {
    int seen;  /* a clearance of this kind is in force in this block */
    int line, col;
    int has_value;
    double value;
} LastClear;

typedef struct {
    const Program *p;
    DiagList *d;
    LastClear last[MAX_TRACKED_FLIGHTS][CL_COUNT];
    /* The commanded level, where it is known. Unlike `last`, this survives
     * an `advance`: advancing moves the aircraft toward the level it was
     * cleared to, it does not forget the clearance. */
    int alt_known[MAX_TRACKED_FLIGHTS];
    double alt_value[MAX_TRACKED_FLIGHTS];
} A;

static void err(A *a, int line, int col, const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_add(a->d, PH_SEMANTIC, SEV_ERROR, line, col, "%s", msg);
}

static void warn(A *a, int line, int col, const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_add(a->d, PH_SEMANTIC, SEV_WARNING, line, col, "%s", msg);
}

static const char *flight_name(A *a, int i, char *buf, size_t n) {
    if (i >= 0 && i < a->p->nflights) return str_cstr(a->p->flights[i].name, buf, n);
    snprintf(buf, n, "aircraft %d", i);
    return buf;
}

/* ---- range checks (SPEC 7.2) --------------------------------------- */

static void check_range(A *a, const Ir *ir, double v, double lo, double hi, const char *what,
                        const char *unit) {
    if (v >= lo && v <= hi) return;
    err(a, ir->line, ir->col, "%s must be between %g and %g %s, found %g", what, lo, hi, unit, v);
}

static void check_clear_range(A *a, const Ir *ir) {
    const ClearInfo *ci = &CLEARS[ir->aux];
    if (ci->operand == TY_FIX) return; /* the type checker already settled it */
    if (ir->b.kind != AR_CONST) return;

    char label[64];
    snprintf(label, sizeof label, "the operand of '%s'", ci->name);
    check_range(a, ir, ir->b.k, ci->lo, ci->hi, label, ci->unit);
}

/* The three values of an `enter` arrive in the preceding `param`
 * instructions, so they are read back from there. */
static void check_enter_range(A *a, const Func *fn, int i) {
    static const struct {
        const char *what;
        double lo, hi;
        const char *unit;
    } limits[3] = {{"an initial altitude", 0, 60000, "ft"},
                   {"an initial heading", 0, 360, "deg"},
                   {"an initial speed", 60, 600, "kt"}};

    int found = 0;
    const Ir *params[3] = {NULL, NULL, NULL};
    for (int j = i - 1; j >= 0 && found < 3; j--) {
        if (fn->ir.items[j].op == IR_NOP || fn->ir.items[j].op == IR_LABEL) continue;
        if (fn->ir.items[j].op != IR_PARAM) break;
        params[2 - found] = &fn->ir.items[j];
        found++;
    }
    if (found != 3) return;

    for (int k = 0; k < 3; k++) {
        if (params[k]->a.kind != AR_CONST) continue; /* only known at runtime */
        check_range(a, &fn->ir.items[i], params[k]->a.k, limits[k].lo, limits[k].hi, limits[k].what,
                    limits[k].unit);
    }
}

static void check_advance_range(A *a, const Ir *ir) {
    if (ir->a.kind != AR_CONST) return;
    if (ir->a.k <= 0) {
        err(a, ir->line, ir->col,
            "'advance' needs a positive number of seconds, found %g; time cannot run backwards",
            ir->a.k);
        return;
    }
    if (ir->a.k > 86400)
        err(a, ir->line, ir->col, "'advance' of %g seconds is longer than the 86400 second limit",
            ir->a.k);
}

/* ---- clearance consistency (SPEC 7.4) ------------------------------ */

static void reset_block(A *a) { memset(a->last, 0, sizeof a->last); }

/* A pair of clearances that assign the same thing, or opposite things, with
 * no `advance` between them. */
static void check_pair(A *a, int f, ClearKind now, ClearKind before, const Ir *ir,
                       const char *why) {
    if (f < 0 || f >= MAX_TRACKED_FLIGHTS) return;
    const LastClear *prev = &a->last[f][before];
    if (!prev->seen) return;
    char nb[32];
    warn(a, ir->line, ir->col, "%s was cleared '%s' at line %d and '%s' here with no 'advance' "
                               "between them: %s",
         flight_name(a, f, nb, sizeof nb), CLEARS[before].name, prev->line, CLEARS[now].name, why);
}

static void check_clearance(A *a, Ir *ir) {
    /* A clearance through a parameter does not say which aircraft it is for,
     * so nothing can be concluded and everything known is discarded. */
    if (ir->a.kind != AR_FLIGHT) {
        reset_block(a);
        memset(a->alt_known, 0, sizeof a->alt_known);
        return;
    }
    int f = ir->a.id;
    if (f < 0 || f >= MAX_TRACKED_FLIGHTS) return;
    ClearKind k = (ClearKind)ir->aux;
    char nb[32];

    switch (k) {
    case CL_CLIMB: check_pair(a, f, k, CL_DESCEND, ir, "the two contradict each other"); break;
    case CL_DESCEND: check_pair(a, f, k, CL_CLIMB, ir, "the two contradict each other"); break;
    case CL_HEADING:
        check_pair(a, f, k, CL_DIRECT, ir, "both assign the lateral path");
        break;
    case CL_DIRECT:
        check_pair(a, f, k, CL_HEADING, ir, "both assign the lateral path");
        break;
    default: break;
    }

    /* The same kind twice over: the first one never takes effect. */
    if (a->last[f][k].seen)
        warn(a, ir->line, ir->col,
             "this '%s' for %s replaces the one at line %d before it can take effect; the first "
             "has no effect",
             CLEARS[k].name, flight_name(a, f, nb, sizeof nb), a->last[f][k].line);

    /* A climb that does not go up, or a descent that does not go down. */
    if ((k == CL_CLIMB || k == CL_DESCEND) && ir->b.kind == AR_CONST) {
        if (a->alt_known[f]) {
            /* Tell the VM this one is already decided, so that the runtime
             * check (which catches the cases only known then) stays quiet. */
            ir->bop = 1;
            /* Strictly, so that re-issuing the level an aircraft is already
             * cleared to -- which a loop naturally does -- is silent. That
             * is a clearance with no effect, not one pointing the wrong way,
             * and the "replaces before it can take effect" check above
             * already covers the case where it really is redundant. */
            double from = a->alt_value[f], to = ir->b.k;
            if (k == CL_CLIMB && to < from)
                warn(a, ir->line, ir->col,
                     "this 'climb' takes %s from %g ft to %g ft, which is not upwards",
                     flight_name(a, f, nb, sizeof nb), from, to);
            if (k == CL_DESCEND && to > from)
                warn(a, ir->line, ir->col,
                     "this 'descend' takes %s from %g ft to %g ft, which is not downwards",
                     flight_name(a, f, nb, sizeof nb), from, to);
        }
        a->alt_known[f] = 1;
        a->alt_value[f] = ir->b.k;
    } else if (k == CL_CLIMB || k == CL_DESCEND) {
        a->alt_known[f] = 0; /* commanded to a value only known at runtime */
    }

    a->last[f][k].seen = 1;
    a->last[f][k].line = ir->line;
    a->last[f][k].col = ir->col;
    if (ir->b.kind == AR_CONST) {
        a->last[f][k].has_value = 1;
        a->last[f][k].value = ir->b.k;
    }
}

/* Seeds the known commanded altitude from an aircraft's initial state. */
static void seed_enter(A *a, const Func *fn, int i) {
    const Ir *ir = &fn->ir.items[i];
    if (ir->a.kind != AR_FLIGHT) return;
    int f = ir->a.id;
    if (f < 0 || f >= MAX_TRACKED_FLIGHTS) return;

    int found = 0;
    const Ir *alt = NULL;
    for (int j = i - 1; j >= 0 && found < 3; j--) {
        if (fn->ir.items[j].op == IR_NOP || fn->ir.items[j].op == IR_LABEL) continue;
        if (fn->ir.items[j].op != IR_PARAM) break;
        if (found == 2) alt = &fn->ir.items[j];
        found++;
    }
    if (found == 3 && alt && alt->a.kind == AR_CONST) {
        a->alt_known[f] = 1;
        a->alt_value[f] = alt->a.k;
    }
}

/* A divisor that only became constant after propagation. The semantic
 * analyser can only see a literal zero, so without this `let z = 0` followed
 * by `1 / z` would reach the runtime instead of the compiler. */
static void check_divisor(A *a, const Ir *ir) {
    if (ir->op != IR_BIN) return;
    if (ir->bop != OP_DIV && ir->bop != OP_MOD) return;
    if (ir->b.kind != AR_CONST || ir->b.k != 0) return;
    err(a, ir->line, ir->col, "%s by zero", ir->bop == OP_DIV ? "division" : "remainder");
}

/* ---- entry point --------------------------------------------------- */

void analyse_ir(FuncList *fns, const Program *p, DiagList *diags) {
    A state = {0};
    A *a = &state;
    a->p = p;
    a->d = diags;

    for (int fi = 0; fi < fns->count; fi++) {
        Func *fn = &fns->items[fi];
        memset(a->alt_known, 0, sizeof a->alt_known);

        for (int b = 0; b < fn->nblocks; b++) {
            if (!fn->blocks[b].reachable) continue;
            reset_block(a);

            for (int i = fn->blocks[b].first; i <= fn->blocks[b].last; i++) {
                Ir *ir = &fn->ir.items[i];
                switch (ir->op) {
                case IR_BIN: check_divisor(a, ir); break;
                case IR_CLEAR:
                    check_clear_range(a, ir);
                    check_clearance(a, ir);
                    break;
                case IR_ENTER:
                    check_enter_range(a, fn, i);
                    seed_enter(a, fn, i);
                    break;
                case IR_ADVANCE:
                    check_advance_range(a, ir);
                    /* Time has passed, so each clearance has had its chance
                     * to take effect and no longer conflicts with the next. */
                    reset_block(a);
                    break;
                case IR_CALL:
                    /* The callee may issue clearances of its own. */
                    reset_block(a);
                    memset(a->alt_known, 0, sizeof a->alt_known);
                    break;
                default: break;
                }
            }
        }
    }
}
