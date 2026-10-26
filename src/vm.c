/*
 * AeroVM: the target machine, the traffic simulation, and the verifier
 * (SPEC section 12).
 *
 * The machine is a register machine with 16 registers per activation record.
 * A call allocates a frame, copies the staged arguments into the callee's
 * first frame slots -- which is where the callee's parameters live, because
 * the slot of virtual register v is v -- and jumps. A return restores the
 * caller's frame. That is the whole runtime organisation: no displays, no
 * static links, no nested procedures.
 *
 * What makes it worth having its own VM rather than generating C is the
 * three things woven into the instruction loop:
 *
 *   - `advance` runs a flight model, so a scenario's timeline is produced by
 *     executing it rather than by the author computing timestamps;
 *   - after every simulated second a separation monitor measures every pair
 *     of aircraft, so a loss of separation is detected wherever it happens;
 *   - `assert` is checked against that simulated state, which is how a
 *     scenario states and verifies its own intent.
 *
 * The flight model is kinematic: aircraft do exactly what they are told at
 * fixed rates, with no mass, wind, performance envelope or pilot response
 * delay. It is good enough to decide when two aircraft come close on the
 * scale of a training scenario and good for nothing else.
 */
#include <math.h>

#include "aerodsl.h"

/* Execution limits (SPEC 12.5). */
#define MAX_STEPS 20000000L
#define MAX_TIME 86400.0
#define MAX_DEPTH 256

/* Flight model rates (SPEC 12.2). */
#define CLIMB_FPS (1800.0 / 60.0)  /* ft per second */
#define DESCEND_FPS (2000.0 / 60.0)
#define TURN_DPS 3.0 /* degrees per second, standard rate */
#define ACCEL_KTS 1.0
#define FIX_REACHED_NM 0.5
#define NM_PER_DEG 60.0

typedef struct {
    int fi;      /* function being executed */
    int ret_pc;  /* where to resume in the caller */
    int ret_fi;
    int base;    /* this frame's first slot in the slot stack */
    double regs[NREGS];
} Frame;

typedef struct {
    const Program *p;
    const FuncList *fns;
    DiagList *d;
    VmResult *res;

    FlightState *fl;
    int nfl;

    double t;
    long steps;
    int error;

    Frame *frames;
    int nframes, frames_cap;
    double *slots;
    int nslots, slots_cap;

    double argbuf[8];

    char *los; /* per ordered pair: currently in loss of separation */

    char pending[256]; /* accumulating one print statement's output */
    int pending_len;

    double next_sample; /* when the next position sample is due */
} VM;

/* How often positions are recorded, and how many samples are kept. The cap
 * stops a day-long scenario from producing an unbounded trace. */
#define SAMPLE_EVERY 10.0
#define MAX_SAMPLES 20000

/* ---- diagnostics and events ---------------------------------------- */

static void rt_error(VM *vm, int line, int col, const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_add(vm->d, PH_RUNTIME, SEV_ERROR, line, col, "%s", msg);
    vm->error = 1;
}

static Event *add_event(VM *vm, EventKind kind, int line, int col) {
    Event e = {0};
    e.kind = kind;
    e.t = vm->t;
    e.flight = e.other = -1;
    e.line = line;
    e.col = col;
    PUSH(vm->res->events, e);
    return &vm->res->events.items[vm->res->events.count - 1];
}

static void flush_print(VM *vm) {
    if (vm->pending_len == 0) return;
    Event *e = add_event(vm, EV_PRINT, 0, 0);
    snprintf(e->text, sizeof e->text, "%s", vm->pending);
    vm->pending[0] = '\0';
    vm->pending_len = 0;
}

static void print_append(VM *vm, const char *s) {
    int room = (int)sizeof vm->pending - vm->pending_len - 1;
    if (room <= 0) return;
    int n = snprintf(vm->pending + vm->pending_len, (size_t)room + 1, "%s", s);
    vm->pending_len += n < room ? n : room;
}

/* Records where every active aircraft is, for drawing the run afterwards. */
static void sample_tracks(VM *vm) {
    if (vm->res->track.count >= MAX_SAMPLES) return;
    for (int i = 0; i < vm->nfl; i++) {
        if (!vm->fl[i].active) continue;
        PUSH(vm->res->track,
             ((TrackPoint){i, vm->t, vm->fl[i].lat, vm->fl[i].lon, vm->fl[i].alt}));
    }
    vm->next_sample = vm->t + SAMPLE_EVERY;
}

/* ---- geometry ------------------------------------------------------ */

static double deg2rad(double d) { return d * 3.14159265358979323846 / 180.0; }
static double rad2deg(double r) { return r * 180.0 / 3.14159265358979323846; }

/*
 * Flat-earth approximation: one degree of latitude is 60 NM, one degree of
 * longitude is 60 NM times the cosine of the latitude. Good to a fraction of
 * a percent over the tens of nautical miles a training scenario spans, and
 * wrong near the poles and across the date line.
 */
static void offsets_nm(double lat1, double lon1, double lat2, double lon2, double *dn,
                       double *de) {
    double mean = deg2rad((lat1 + lat2) / 2.0);
    *dn = (lat2 - lat1) * NM_PER_DEG;
    *de = (lon2 - lon1) * NM_PER_DEG * cos(mean);
}

static double hsep_nm(const FlightState *a, const FlightState *b) {
    double dn, de;
    offsets_nm(a->lat, a->lon, b->lat, b->lon, &dn, &de);
    return sqrt(dn * dn + de * de);
}

static double vsep_ft(const FlightState *a, const FlightState *b) {
    return fabs(a->alt - b->alt);
}

static double dist_to(const FlightState *f, double lat, double lon) {
    double dn, de;
    offsets_nm(f->lat, f->lon, lat, lon, &dn, &de);
    return sqrt(dn * dn + de * de);
}

static double bearing_to(const FlightState *f, double lat, double lon) {
    double dn, de;
    offsets_nm(f->lat, f->lon, lat, lon, &dn, &de);
    if (dn == 0 && de == 0) return f->hdg;
    double b = rad2deg(atan2(de, dn));
    while (b < 0) b += 360.0;
    while (b >= 360.0) b -= 360.0;
    return b;
}

/* Signed difference from a to b, in (-180, 180]. */
static double angle_diff(double from, double to) {
    double d = to - from;
    while (d > 180.0) d -= 360.0;
    while (d <= -180.0) d += 360.0;
    return d;
}

static double norm_hdg(double h) {
    while (h < 0) h += 360.0;
    while (h >= 360.0) h -= 360.0;
    return h;
}

/* ---- simulation ---------------------------------------------------- */

static void monitor_separation(VM *vm) {
    for (int i = 0; i < vm->nfl; i++) {
        if (!vm->fl[i].active) continue;
        for (int j = i + 1; j < vm->nfl; j++) {
            if (!vm->fl[j].active) continue;

            double h = hsep_nm(&vm->fl[i], &vm->fl[j]);
            double v = vsep_ft(&vm->fl[i], &vm->fl[j]);

            if (h < vm->res->min_hsep) {
                vm->res->min_hsep = h;
                vm->res->min_vsep = v;
                vm->res->min_pair_a = i;
                vm->res->min_pair_b = j;
            }

            int in_los = h < SEP_H_NM && v < SEP_V_FT;
            char *was = &vm->los[i * vm->nfl + j];
            if (in_los && !*was) {
                Event *e = add_event(vm, EV_LOS, 0, 0);
                e->flight = i;
                e->other = j;
                e->value = h;
                char a[32], b[32];
                snprintf(e->text, sizeof e->text,
                         "%s and %s: %.2f NM apart, %.0f ft vertically",
                         str_cstr(vm->fl[i].name, a, sizeof a),
                         str_cstr(vm->fl[j].name, b, sizeof b), h, v);
                vm->res->los_count++;
            }
            *was = (char)in_los;
        }
    }
}

static void step_flight(FlightState *f, const Program *p, double dt) {
    /* Vertical. */
    double dalt = f->t_alt - f->alt;
    double rate = (dalt > 0 ? CLIMB_FPS : DESCEND_FPS) * dt;
    if (fabs(dalt) <= rate)
        f->alt = f->t_alt;
    else
        f->alt += dalt > 0 ? rate : -rate;

    /* Lateral: tracking a waypoint steers continuously toward it. */
    if (f->direct_fix >= 0 && f->direct_fix < p->nfixes) {
        const FixDecl *fx = &p->fixes[f->direct_fix];
        f->t_hdg = bearing_to(f, fx->lat, fx->lon);
    }
    double dh = angle_diff(f->hdg, f->t_hdg);
    double turn = TURN_DPS * dt;
    if (fabs(dh) <= turn)
        f->hdg = f->t_hdg;
    else
        f->hdg = norm_hdg(f->hdg + (dh > 0 ? turn : -turn));

    /* Speed. */
    double dspd = f->t_spd - f->spd;
    double accel = ACCEL_KTS * dt;
    if (fabs(dspd) <= accel)
        f->spd = f->t_spd;
    else
        f->spd += dspd > 0 ? accel : -accel;

    /* Position. */
    double nm = f->spd * dt / 3600.0;
    double lat_rad = deg2rad(f->lat);
    f->lat += nm * cos(deg2rad(f->hdg)) / NM_PER_DEG;
    double coslat = cos(lat_rad);
    if (fabs(coslat) > 1e-9) f->lon += nm * sin(deg2rad(f->hdg)) / (NM_PER_DEG * coslat);

    if (f->direct_fix >= 0 && f->direct_fix < p->nfixes) {
        const FixDecl *fx = &p->fixes[f->direct_fix];
        if (dist_to(f, fx->lat, fx->lon) < FIX_REACHED_NM) f->direct_fix = -1;
    }
}

static void do_advance(VM *vm, double secs, int line, int col) {
    if (!(secs > 0)) {
        rt_error(vm, line, col, "'advance' needs a positive number of seconds, got %g", secs);
        return;
    }
    if (secs > MAX_TIME) {
        rt_error(vm, line, col, "'advance' of %g seconds exceeds the %g second limit", secs,
                 MAX_TIME);
        return;
    }

    double left = secs;
    while (left > 0 && !vm->error) {
        double dt = left > 1.0 ? 1.0 : left;
        for (int i = 0; i < vm->nfl; i++)
            if (vm->fl[i].active) step_flight(&vm->fl[i], vm->p, dt);
        vm->t += dt;
        left -= dt;

        monitor_separation(vm);
        if (vm->t >= vm->next_sample) sample_tracks(vm);

        if (vm->t > MAX_TIME) {
            rt_error(vm, line, col,
                     "the scenario has run past the %g second limit of simulated time", MAX_TIME);
            return;
        }
    }
}

static void rt_warn(VM *vm, int line, int col, const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_add(vm->d, PH_RUNTIME, SEV_WARNING, line, col, "%s", msg);
}

/*
 * `checked` is set when src/analysis.c already decided whether this climb
 * climbs. It could only do that where the level was known at compile time,
 * so this is where the rest are caught -- and the flag is what stops the
 * same clearance being reported twice.
 */
static void do_clear(VM *vm, int flight, ClearKind kind, double value, int checked, int line,
                     int col) {
    if (flight < 0 || flight >= vm->nfl) {
        rt_error(vm, line, col, "a clearance was issued to an aircraft that does not exist");
        return;
    }
    FlightState *f = &vm->fl[flight];
    char buf[32];

    if (!f->active) {
        rt_error(vm, line, col, "%s has not entered the airspace yet",
                 str_cstr(f->name, buf, sizeof buf));
        return;
    }

    const ClearInfo *ci = &CLEARS[kind];
    if (kind == CL_DIRECT) {
        int fx = (int)value;
        if (fx < 0 || fx >= vm->p->nfixes) {
            rt_error(vm, line, col, "'direct' names a waypoint that does not exist");
            return;
        }
        f->direct_fix = fx;
    } else {
        if (value < ci->lo || value > ci->hi) {
            rt_error(vm, line, col, "'%s' must be between %g and %g %s, got %g", ci->name, ci->lo,
                     ci->hi, ci->unit, value);
            return;
        }
        switch (kind) {
        case CL_CLIMB:
        case CL_DESCEND:
            if (!checked) {
                if (kind == CL_CLIMB && value < f->t_alt)
                    rt_warn(vm, line, col,
                            "this 'climb' takes %s from %g ft to %g ft, which is not upwards",
                            str_cstr(f->name, buf, sizeof buf), f->t_alt, value);
                else if (kind == CL_DESCEND && value > f->t_alt)
                    rt_warn(vm, line, col,
                            "this 'descend' takes %s from %g ft to %g ft, which is not downwards",
                            str_cstr(f->name, buf, sizeof buf), f->t_alt, value);
            }
            f->t_alt = value;
            break;
        case CL_HEADING:
            f->t_hdg = norm_hdg(value);
            f->direct_fix = -1; /* a heading replaces waypoint tracking */
            break;
        case CL_SPEED: f->t_spd = value; break;
        default: break;
        }
    }

    Event *e = add_event(vm, EV_CLEAR, line, col);
    e->flight = flight;
    e->clear = kind;
    e->value = value;
}

static void do_enter(VM *vm, int flight, double alt, double hdg, double spd, int line, int col) {
    if (flight < 0 || flight >= vm->nfl) return;
    FlightState *f = &vm->fl[flight];
    char buf[32];

    if (f->active) {
        rt_error(vm, line, col, "%s has already entered the airspace",
                 str_cstr(f->name, buf, sizeof buf));
        return;
    }
    struct {
        double v;
        double lo, hi;
        const char *what, *unit;
    } checks[] = {{alt, 0, 60000, "an initial altitude", "ft"},
                  {hdg, 0, 360, "an initial heading", "deg"},
                  {spd, 60, 600, "an initial speed", "kt"}};
    for (int i = 0; i < LEN(checks); i++) {
        if (checks[i].v >= checks[i].lo && checks[i].v <= checks[i].hi) continue;
        rt_error(vm, line, col, "%s must be between %g and %g %s, got %g", checks[i].what,
                 checks[i].lo, checks[i].hi, checks[i].unit, checks[i].v);
        return;
    }

    f->alt = f->t_alt = alt;
    f->hdg = f->t_hdg = norm_hdg(hdg);
    f->spd = f->t_spd = spd;
    f->direct_fix = -1;
    f->active = 1;

    Event *e = add_event(vm, EV_ENTER, line, col);
    e->flight = flight;
    /* The scenario-file backend needs the state as it was on entry, which
     * only the VM knows, so it is formatted here and carried on the event. */
    {
        char altbuf[24], ty[24];
        const Str *t = &vm->p->flights[flight].type;
        snprintf(ty, sizeof ty, "%s", t->ptr && t->len ? str_cstr(*t, buf, sizeof buf) : "A320");
        scn_format_alt(f->alt, altbuf, sizeof altbuf);
        snprintf(e->text, sizeof e->text, "%s,%.6f,%.6f,%.0f,%s,%.0f", ty, f->lat, f->lon, f->hdg,
                 altbuf, f->spd);
    }

    /* An aircraft can enter already in conflict with one that is airborne,
     * so the monitor runs on entry as well as after each simulated second. */
    monitor_separation(vm);
    sample_tracks(vm);
}

/* ---- built-ins ----------------------------------------------------- */

static double call_builtin(VM *vm, Builtin fn, double a, double b, int line, int col) {
    int ia = (int)a, ib = (int)b;
    switch (fn) {
    case BI_VSEP:
    case BI_HSEP:
        if (ia < 0 || ia >= vm->nfl || ib < 0 || ib >= vm->nfl) {
            rt_error(vm, line, col, "%s was given an aircraft that does not exist",
                     BUILTINS[fn].name);
            return 0;
        }
        return fn == BI_VSEP ? vsep_ft(&vm->fl[ia], &vm->fl[ib]) : hsep_nm(&vm->fl[ia], &vm->fl[ib]);
    case BI_DIST:
    case BI_BEARING:
        if (ia < 0 || ia >= vm->nfl || ib < 0 || ib >= vm->p->nfixes) {
            rt_error(vm, line, col, "%s was given an aircraft or waypoint that does not exist",
                     BUILTINS[fn].name);
            return 0;
        }
        return fn == BI_DIST ? dist_to(&vm->fl[ia], vm->p->fixes[ib].lat, vm->p->fixes[ib].lon)
                             : bearing_to(&vm->fl[ia], vm->p->fixes[ib].lat, vm->p->fixes[ib].lon);
    case BI_ABS: return fabs(a);
    case BI_MIN: return a < b ? a : b;
    case BI_MAX: return a > b ? a : b;
    case BI_NOW: return vm->t;
    default: return 0;
    }
}

/* ---- arithmetic ---------------------------------------------------- */

static double do_bin(VM *vm, BinOp op, double a, double b, int line, int col) {
    double r;
    switch (op) {
    case OP_ADD: r = a + b; break;
    case OP_SUB: r = a - b; break;
    case OP_MUL: r = a * b; break;
    case OP_DIV:
        if (b == 0) {
            rt_error(vm, line, col, "division by zero");
            return 0;
        }
        r = a / b;
        break;
    case OP_MOD:
        if (b == 0) {
            rt_error(vm, line, col, "remainder by zero");
            return 0;
        }
        r = fmod(a, b);
        break;
    case OP_LT: return a < b;
    case OP_LE: return a <= b;
    case OP_GT: return a > b;
    case OP_GE: return a >= b;
    case OP_EQ: return a == b;
    case OP_NE: return a != b;
    default: return 0;
    }
    /* Rejecting non-finite results is what lets the optimiser treat x+0,
     * x-0, x*1 and x/1 as exact identities (see src/opt.c). */
    if (!isfinite(r)) {
        rt_error(vm, line, col, "arithmetic overflow");
        return 0;
    }
    return r;
}

static int rel_holds(BinOp op, double a, double b) {
    switch (op) {
    case OP_LT: return a < b;
    case OP_LE: return a <= b;
    case OP_GT: return a > b;
    case OP_GE: return a >= b;
    case OP_EQ: return a == b;
    case OP_NE: return a != b;
    default: return 0;
    }
}

/* ---- the interpreter ----------------------------------------------- */

static void ensure_slots(VM *vm, int need) {
    if (need <= vm->slots_cap) return;
    int cap = vm->slots_cap ? vm->slots_cap : 64;
    while (cap < need) cap *= 2;
    vm->slots = xrealloc(vm->slots, (size_t)cap * sizeof *vm->slots);
    for (int i = vm->slots_cap; i < cap; i++) vm->slots[i] = 0;
    vm->slots_cap = cap;
}

static void push_frame(VM *vm, int fi, int ret_fi, int ret_pc, int base) {
    Frame f = {0};
    f.fi = fi;
    f.ret_fi = ret_fi;
    f.ret_pc = ret_pc;
    f.base = base;
    PUSHN(vm->frames, vm->nframes, vm->frames_cap, f);
}

void vm_run(const Program *p, const FuncList *fns, VmResult *out, DiagList *diags) {
    memset(out, 0, sizeof *out);
    out->min_hsep = 1e30;
    out->min_vsep = 1e30;
    out->min_pair_a = out->min_pair_b = -1;

    VM vm = {0};
    vm.p = p;
    vm.fns = fns;
    vm.d = diags;
    vm.res = out;
    vm.nfl = p->nflights;

    vm.fl = xcalloc((size_t)(vm.nfl > 0 ? vm.nfl : 1), sizeof *vm.fl);
    vm.los = xcalloc((size_t)(vm.nfl > 0 ? vm.nfl * vm.nfl : 1), 1);
    for (int i = 0; i < vm.nfl; i++) {
        vm.fl[i].name = p->flights[i].name;
        vm.fl[i].direct_fix = -1;
        int fx = p->flights[i].fix;
        if (fx >= 0 && fx < p->nfixes) {
            vm.fl[i].lat = p->fixes[fx].lat;
            vm.fl[i].lon = p->fixes[fx].lon;
        }
    }

    if (fns->count == 0) goto done;

    ensure_slots(&vm, fns->items[0].nslots + 8);
    vm.nslots = fns->items[0].nslots;
    push_frame(&vm, 0, -1, -1, 0);

    int fi = 0, pc = 0;
    while (!vm.error) {
        if (++vm.steps > MAX_STEPS) {
            rt_error(&vm, 0, 0,
                     "the scenario executed more than %ld instructions and was stopped; it "
                     "probably contains a loop that cannot finish",
                     MAX_STEPS);
            break;
        }

        const Func *fn = &fns->items[fi];
        if (pc < 0 || pc >= fn->nbc) {
            /* Falling off the end of a procedure returns from it. */
            if (vm.nframes <= 1) break;
            Frame *cur = &vm.frames[vm.nframes - 1];
            fi = cur->ret_fi;
            pc = cur->ret_pc;
            vm.nframes--;
            if (fi < 0) break;
            continue;
        }

        const Bc *b = &fn->bc[pc];
        Frame *f = &vm.frames[vm.nframes - 1];
        double *r = f->regs;
        int base = f->base;

        switch (b->op) {
        case BC_LOADK: r[b->a] = b->k; break;
        case BC_MOVE: r[b->a] = r[b->b]; break;
        case BC_BIN: r[b->a] = do_bin(&vm, b->bop, r[b->b], r[b->c], b->line, b->col); break;
        case BC_UN:
            r[b->a] = b->bop == OP_NEG ? -r[b->b] : (r[b->b] == 0 ? 1 : 0);
            break;
        case BC_LOAD: r[b->a] = vm.slots[base + b->b]; break;
        case BC_STORE: vm.slots[base + b->a] = r[b->b]; break;
        case BC_JMP: pc = b->a; continue;
        case BC_JT:
            if (r[b->b] != 0) {
                pc = b->a;
                continue;
            }
            break;
        case BC_JF:
            if (r[b->b] == 0) {
                pc = b->a;
                continue;
            }
            break;
        case BC_JREL:
            if (rel_holds(b->bop, r[b->b], r[b->c])) {
                pc = b->a;
                continue;
            }
            break;

        case BC_FIELD: {
            int idx = (int)r[b->b];
            if (idx < 0 || idx >= vm.nfl) {
                rt_error(&vm, b->line, b->col, "a field was read from an aircraft that does not "
                                               "exist");
                break;
            }
            const FlightState *s = &vm.fl[idx];
            switch ((Field)b->aux) {
            case FLD_ALT: r[b->a] = s->alt; break;
            case FLD_HDG: r[b->a] = s->hdg; break;
            case FLD_SPD: r[b->a] = s->spd; break;
            case FLD_LAT: r[b->a] = s->lat; break;
            case FLD_LON: r[b->a] = s->lon; break;
            default: r[b->a] = 0; break;
            }
            break;
        }

        case BC_BI:
            r[b->a] = call_builtin(&vm, (Builtin)b->aux, r[b->b], r[b->c], b->line, b->col);
            break;

        case BC_ENTER:
            do_enter(&vm, b->a, vm.argbuf[0], vm.argbuf[1], vm.argbuf[2], b->line, b->col);
            break;

        case BC_CLEAR:
            do_clear(&vm, (int)r[b->b], (ClearKind)b->aux, r[b->c], b->a, b->line, b->col);
            break;

        case BC_ADV: do_advance(&vm, r[b->b], b->line, b->col); break;

        case BC_ASSERT: {
            int held = r[b->b] != 0;
            out->asserts_run++;
            Event *e = add_event(&vm, held ? EV_ASSERT_OK : EV_ASSERT_FAIL, b->line, b->col);
            const char *msg = (b->aux >= 0 && b->aux < p->nstrings) ? p->strings[b->aux] : NULL;
            snprintf(e->text, sizeof e->text, "%s", msg ? msg : "assertion");
            if (!held) {
                out->asserts_failed++;
                if (msg)
                    diag_add(diags, PH_VERIFY, SEV_ERROR, b->line, b->col,
                             "assertion failed at %.0f s: %s", vm.t, msg);
                else
                    diag_add(diags, PH_VERIFY, SEV_ERROR, b->line, b->col,
                             "assertion failed at %.0f s", vm.t);
            }
            break;
        }

        case BC_PRINT: {
            char buf[64];
            double v = r[b->b];
            switch ((Type)b->aux) {
            case TY_BOOL: snprintf(buf, sizeof buf, "%s", v != 0 ? "true" : "false"); break;
            case TY_FLIGHT: {
                int i = (int)v;
                if (i >= 0 && i < vm.nfl)
                    str_cstr(vm.fl[i].name, buf, sizeof buf);
                else
                    snprintf(buf, sizeof buf, "<aircraft %d>", i);
                break;
            }
            case TY_FIX: {
                int i = (int)v;
                if (i >= 0 && i < p->nfixes)
                    str_cstr(p->fixes[i].name, buf, sizeof buf);
                else
                    snprintf(buf, sizeof buf, "<waypoint %d>", i);
                break;
            }
            default: snprintf(buf, sizeof buf, "%g", v); break;
            }
            print_append(&vm, buf);
            if (b->c) flush_print(&vm);
            break;
        }

        case BC_PRINTS:
            if (b->aux >= 0 && b->aux < p->nstrings) print_append(&vm, p->strings[b->aux]);
            if (b->c) flush_print(&vm);
            break;

        case BC_ARG:
            if (b->a >= 0 && b->a < LEN(vm.argbuf)) vm.argbuf[b->a] = r[b->b];
            break;

        case BC_CALL: {
            if (vm.nframes >= MAX_DEPTH) {
                rt_error(&vm, b->line, b->col,
                         "procedure calls are nested more than %d deep; a procedure is probably "
                         "calling itself without end",
                         MAX_DEPTH);
                break;
            }
            if (b->a < 0 || b->a >= fns->count) {
                rt_error(&vm, b->line, b->col, "a call names a procedure that does not exist");
                break;
            }
            const Func *callee = &fns->items[b->a];
            int newbase = base + fn->nslots;
            ensure_slots(&vm, newbase + callee->nslots + 8);
            for (int i = 0; i < callee->nslots; i++) vm.slots[newbase + i] = 0;
            for (int i = 0; i < b->b && i < LEN(vm.argbuf); i++) vm.slots[newbase + i] = vm.argbuf[i];
            push_frame(&vm, b->a, fi, pc + 1, newbase);
            fi = b->a;
            pc = 0;
            continue;
        }

        case BC_RET: {
            if (vm.nframes <= 1) goto halt;
            Frame *cur = &vm.frames[vm.nframes - 1];
            int rfi = cur->ret_fi, rpc = cur->ret_pc;
            vm.nframes--;
            if (rfi < 0) goto halt;
            fi = rfi;
            pc = rpc;
            continue;
        }

        case BC_HALT: goto halt;
        }
        pc++;
    }

halt:
    flush_print(&vm);
    sample_tracks(&vm); /* a final position for every aircraft */

done:
    out->t = vm.t;
    out->steps = (int)(vm.steps > 2000000000L ? 2000000000L : vm.steps);
    out->flights = vm.fl;
    out->nflights = vm.nfl;
    if (out->min_hsep > 1e29) {
        out->min_hsep = 0;
        out->min_vsep = 0;
    }

    free(vm.frames);
    free(vm.slots);
    free(vm.los);
}

void vm_result_free(VmResult *r) {
    free(r->track.items);
    free(r->events.items);
    free(r->flights);
    memset(r, 0, sizeof *r);
}

/* ---- trace ---------------------------------------------------------- */

static void fmt_time(double t, char *buf, size_t n) {
    int total = (int)(t + 0.5);
    snprintf(buf, n, "%02d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
}

void vm_print_trace(FILE *f, const Program *p, const VmResult *r) {
    char tb[16], nb[32];
    fprintf(f, "simulation trace (%d event%s, %.0f s of simulated time, %d instructions)\n",
            r->events.count, r->events.count == 1 ? "" : "s", r->t, r->steps);

    for (int i = 0; i < r->events.count; i++) {
        const Event *e = &r->events.items[i];
        fmt_time(e->t, tb, sizeof tb);
        fprintf(f, "  %s  ", tb);
        switch (e->kind) {
        case EV_ENTER:
            fprintf(f, "%s enters the airspace", str_cstr(p->flights[e->flight].name, nb, sizeof nb));
            break;
        case EV_CLEAR:
            fprintf(f, "%s cleared %s ", str_cstr(p->flights[e->flight].name, nb, sizeof nb),
                    CLEARS[e->clear].name);
            if (e->clear == CL_DIRECT) {
                int fx = (int)e->value;
                fprintf(f, "%s", fx >= 0 && fx < p->nfixes
                                     ? str_cstr(p->fixes[fx].name, nb, sizeof nb)
                                     : "?");
            } else {
                fprintf(f, "%g %s", e->value, CLEARS[e->clear].unit);
            }
            break;
        case EV_LOS: fprintf(f, "LOSS OF SEPARATION: %s", e->text); break;
        case EV_ASSERT_OK: fprintf(f, "assertion held: %s", e->text); break;
        case EV_ASSERT_FAIL: fprintf(f, "ASSERTION FAILED: %s", e->text); break;
        case EV_PRINT: fprintf(f, "print: %s", e->text); break;
        }
        fputc('\n', f);
    }

    fputs("\nfinal state\n", f);
    for (int i = 0; i < r->nflights; i++) {
        const FlightState *s = &r->flights[i];
        fprintf(f, "  %-8s", str_cstr(s->name, nb, sizeof nb));
        if (!s->active) {
            fputs("never entered the airspace\n", f);
            continue;
        }
        fprintf(f, "alt %6.0f ft  hdg %3.0f  spd %3.0f kt  at %.4f %.4f\n", s->alt, s->hdg, s->spd,
                s->lat, s->lon);
    }

    if (r->min_pair_a >= 0)
        fprintf(f, "\nclosest approach: %s and %s, %.2f NM apart with %.0f ft vertically\n",
                str_cstr(p->flights[r->min_pair_a].name, nb, sizeof nb),
                str_cstr(p->flights[r->min_pair_b].name, tb, sizeof tb), r->min_hsep, r->min_vsep);
    fprintf(f, "separation standard applied: %.0f NM horizontally and %.0f ft vertically\n",
            SEP_H_NM, SEP_V_FT);
    fprintf(f, "losses of separation: %d\n", r->los_count);
    if (r->asserts_run)
        fprintf(f, "assertions: %d checked, %d failed\n", r->asserts_run, r->asserts_failed);
}
