/*
 * Dumps every phase of a compilation as one JSON object, for the
 * demonstration UI.
 *
 * The phases that are naturally textual -- the AST, the symbol table, the
 * three-address code, the control-flow graph, the bytecode, the trace, the
 * scenario file -- are captured as strings by running the same printers the
 * command line uses, so there is exactly one implementation of each. The
 * phases the UI wants to style individually -- tokens, diagnostics, events,
 * flights -- are emitted as structured JSON.
 */
#include "aerodsl.h"

static void json_string(FILE *out, const char *s, int len) {
    fputc('"', out);
    for (int i = 0; len < 0 ? s[i] != '\0' : i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\n': fputs("\\n", out); break;
        case '\r': fputs("\\r", out); break;
        case '\t': fputs("\\t", out); break;
        default:
            if (c < 0x20)
                fprintf(out, "\\u%04x", c);
            else
                fputc(c, out);
        }
    }
    fputc('"', out);
}

static void json_str(FILE *out, Str s) { json_string(out, s.ptr ? s.ptr : "", s.len); }

static void json_num(FILE *out, double v) {
    if (v != v || v > 1e300 || v < -1e300)
        fputs("null", out); /* JSON has no NaN or infinity */
    else
        fprintf(out, "%.10g", v);
}

/* Runs a printer into a string. */
typedef void (*Printer)(FILE *, void *);

static char *capture(Printer fn, void *arg) {
    MemOut m;
    FILE *mem = memout_open(&m);
    if (!mem) return NULL;
    fn(mem, arg);
    return memout_close(&m, NULL);
}

/* ---- printer adaptors ---------------------------------------------- */

typedef struct {
    Unit *u; /* mutable: the SSA printer recomputes the dominator info */
    int which;
} Ctx;

static void print_ast_a(FILE *f, void *p) { ast_print(f, &((Ctx *)p)->u->prog); }
static void print_syms_a(FILE *f, void *p) { symtab_print(f, &((Ctx *)p)->u->st); }

static void print_ir_a(FILE *f, void *p) {
    const Unit *u = ((Ctx *)p)->u;
    ir_print_all(f, &u->prog, &u->funcs);
}

static void print_optir_a(FILE *f, void *p) {
    const Unit *u = ((Ctx *)p)->u;
    ir_print_all(f, &u->prog, &u->opt_funcs);
}

static void print_cfg_a(FILE *f, void *p) {
    const Unit *u = ((Ctx *)p)->u;
    for (int i = 0; i < u->opt_funcs.count; i++) {
        if (i) fputc('\n', f);
        cfg_print(f, &u->opt_funcs.items[i]);
    }
}

static void print_ssa_a(FILE *f, void *p) {
    Unit *u = (Unit *)((Ctx *)p)->u;
    for (int i = 0; i < u->opt_funcs.count; i++) {
        if (i) fputc('\n', f);
        ssa_print(f, &u->prog, &u->opt_funcs.items[i]);
    }
}

static void print_nextuse_a(FILE *f, void *p) {
    const Unit *u = ((Ctx *)p)->u;
    for (int i = 0; i < u->opt_funcs.count; i++) {
        if (i) fputc('\n', f);
        nextuse_print(f, &u->opt_funcs.items[i]);
    }
}

static void print_bc_a(FILE *f, void *p) {
    const Unit *u = ((Ctx *)p)->u;
    bc_print_all(f, &u->prog, &u->opt_funcs);
}

static void print_trace_a(FILE *f, void *p) {
    const Unit *u = ((Ctx *)p)->u;
    vm_print_trace(f, &u->prog, &u->vm);
}

static void print_scn_a(FILE *f, void *p) {
    const Unit *u = ((Ctx *)p)->u;
    scn_emit(f, &u->prog, &u->vm);
}

/* ---- the dump ------------------------------------------------------ */

static void emit_text_field(FILE *out, const char *name, Printer fn, Ctx *ctx, int available,
                            int *first) {
    fprintf(out, "%s\n    ", *first ? "" : ",");
    *first = 0;
    json_string(out, name, -1);
    fputs(": ", out);
    if (!available) {
        fputs("null", out);
        return;
    }
    char *text = capture(fn, ctx);
    json_string(out, text ? text : "", -1);
    free(text);
}

void json_dump(FILE *out, const Unit *u, const char *src) {
    Ctx ctx = {(Unit *)u, 0};
    char buf[128];

    fputs("{\n  \"scenario\": ", out);
    json_str(out, u->prog.scenario);

    fprintf(out, ",\n  \"ok\": %s", diag_errors(&u->diags) ? "false" : "true");
    fprintf(out,
            ",\n  \"phases\": {\"lexed\": %s, \"parsed\": %s, \"checked\": %s, "
            "\"lowered\": %s, \"ran\": %s}",
            u->lexed ? "true" : "false", u->parsed ? "true" : "false",
            u->checked ? "true" : "false", u->lowered ? "true" : "false",
            u->ran ? "true" : "false");

    fputs(",\n  \"source\": ", out);
    json_string(out, src ? src : "", -1);

    /* diagnostics */
    fputs(",\n  \"diagnostics\": [", out);
    for (int i = 0; i < u->diags.count; i++) {
        const Diagnostic *d = &u->diags.items[i];
        fprintf(out, "%s\n    {\"phase\": \"%s\", \"severity\": \"%s\", \"line\": %d, "
                     "\"col\": %d, \"message\": ",
                i ? "," : "", phase_name(d->phase), sev_name(d->sev), d->line, d->col);
        json_string(out, d->message, -1);
        fputc('}', out);
    }
    fputs("\n  ]", out);

    /* tokens */
    fputs(",\n  \"tokens\": [", out);
    for (int i = 0; i < u->tokens.count; i++) {
        const Token *t = &u->tokens.items[i];
        fprintf(out, "%s\n    {\"type\": \"%s\", \"text\": ", i ? "," : "", token_name(t->type));
        json_str(out, t->text);
        fprintf(out, ", \"line\": %d, \"col\": %d", t->line, t->col);
        if (t->type == T_NUMBER || t->type == T_FLEVEL || t->type == T_SPEED) {
            fputs(", \"value\": ", out);
            json_num(out, t->num);
        }
        fputc('}', out);
    }
    fputs("\n  ]", out);

    /* optimisation counts */
    const OptStats *s = &u->stats;
    fprintf(out,
            ",\n  \"stats\": {\"folded\": %d, \"propagated\": %d, \"cse\": %d, \"copies\": %d, "
            "\"algebraic\": %d, \"dead\": %d, \"hoisted\": %d, \"jumps\": %d, "
            "\"blocksRemoved\": %d, \"peephole\": %d}",
            s->folded, s->propagated, s->cse, s->copies, s->algebraic, s->dead, s->hoisted,
            s->jumps, s->blocks_removed, s->peephole);

    /* instruction counts, so the UI can show what the optimiser removed */
    int before = 0, after = 0, bc = 0;
    for (int i = 0; i < u->funcs.count; i++) before += u->funcs.items[i].ir.count;
    for (int i = 0; i < u->opt_funcs.count; i++) {
        after += u->opt_funcs.items[i].ir.count;
        bc += u->opt_funcs.items[i].nbc;
    }
    fprintf(out, ",\n  \"sizes\": {\"irBefore\": %d, \"irAfter\": %d, \"bytecode\": %d}", before,
            after, bc);

    /* simulation */
    const VmResult *r = &u->vm;
    fputs(",\n  \"vm\": {", out);
    fputs("\"time\": ", out);
    json_num(out, r->t);
    fprintf(out, ", \"steps\": %d, \"losses\": %d, \"assertsRun\": %d, \"assertsFailed\": %d",
            r->steps, r->los_count, r->asserts_run, r->asserts_failed);
    fputs(", \"minHsep\": ", out);
    json_num(out, r->min_hsep);
    fputs(", \"minVsep\": ", out);
    json_num(out, r->min_vsep);
    fprintf(out, ", \"sepStandardNm\": %g, \"sepStandardFt\": %g", SEP_H_NM, SEP_V_FT);
    fputc('}', out);

    /* events */
    static const char *const EV_NAMES[] = {"clear", "enter", "los", "assertOk", "assertFail",
                                           "print"};
    fputs(",\n  \"events\": [", out);
    for (int i = 0; i < r->events.count; i++) {
        const Event *e = &r->events.items[i];
        fprintf(out, "%s\n    {\"kind\": \"%s\", \"t\": ", i ? "," : "", EV_NAMES[e->kind]);
        json_num(out, e->t);
        fprintf(out, ", \"line\": %d, \"col\": %d", e->line, e->col);
        if (e->flight >= 0 && e->flight < u->prog.nflights) {
            fputs(", \"flight\": ", out);
            json_str(out, u->prog.flights[e->flight].name);
        }
        if (e->other >= 0 && e->other < u->prog.nflights) {
            fputs(", \"other\": ", out);
            json_str(out, u->prog.flights[e->other].name);
        }
        if (e->kind == EV_CLEAR) {
            fprintf(out, ", \"clearance\": \"%s\", \"unit\": \"%s\", \"value\": ",
                    CLEARS[e->clear].name, CLEARS[e->clear].unit);
            if (e->clear == CL_DIRECT) {
                int fx = (int)e->value;
                if (fx >= 0 && fx < u->prog.nfixes)
                    json_str(out, u->prog.fixes[fx].name);
                else
                    fputs("null", out);
            } else {
                json_num(out, e->value);
            }
        }
        if (e->text[0]) {
            fputs(", \"text\": ", out);
            json_string(out, e->text, -1);
        }
        fputc('}', out);
    }
    fputs("\n  ]", out);

    /* final flight state */
    fputs(",\n  \"flights\": [", out);
    for (int i = 0; i < r->nflights; i++) {
        const FlightState *fl = &r->flights[i];
        fprintf(out, "%s\n    {\"name\": ", i ? "," : "");
        json_str(out, fl->name);
        fprintf(out, ", \"active\": %s", fl->active ? "true" : "false");
        const char *keys[] = {"alt", "hdg", "spd", "lat", "lon"};
        double vals[] = {fl->alt, fl->hdg, fl->spd, fl->lat, fl->lon};
        for (int k = 0; k < 5; k++) {
            fprintf(out, ", \"%s\": ", keys[k]);
            json_num(out, vals[k]);
        }
        fputc('}', out);
    }
    fputs("\n  ]", out);

    /* sampled positions, so a demonstration can draw the tracks */
    fputs(",\n  \"tracks\": [", out);
    for (int i = 0; i < r->track.count; i++) {
        const TrackPoint *tp = &r->track.items[i];
        fprintf(out, "%s[%d,%.1f,%.5f,%.5f,%.0f]", i ? "," : "", tp->flight, tp->t, tp->lat,
                tp->lon, tp->alt);
    }
    fputs("]", out);

    /* declared waypoints, so the UI can draw a map */
    fputs(",\n  \"fixes\": [", out);
    for (int i = 0; i < u->prog.nfixes; i++) {
        fprintf(out, "%s\n    {\"name\": ", i ? "," : "");
        json_str(out, u->prog.fixes[i].name);
        fprintf(out, ", \"lat\": %.6f, \"lon\": %.6f}", u->prog.fixes[i].lat,
                u->prog.fixes[i].lon);
    }
    fputs("\n  ]", out);

    /* the textual phases */
    fputs(",\n  \"text\": {", out);
    int first = 1;
    emit_text_field(out, "ast", print_ast_a, &ctx, u->parsed, &first);
    emit_text_field(out, "symbols", print_syms_a, &ctx, u->parsed, &first);
    emit_text_field(out, "ir", print_ir_a, &ctx, u->checked, &first);
    emit_text_field(out, "cfg", print_cfg_a, &ctx, u->checked, &first);
    emit_text_field(out, "optIr", print_optir_a, &ctx, u->checked, &first);
    emit_text_field(out, "ssa", print_ssa_a, &ctx, u->checked, &first);
    emit_text_field(out, "nextuse", print_nextuse_a, &ctx, u->lowered, &first);
    emit_text_field(out, "bytecode", print_bc_a, &ctx, u->lowered, &first);
    emit_text_field(out, "trace", print_trace_a, &ctx, u->ran, &first);
    emit_text_field(out, "scn", print_scn_a, &ctx, u->ran, &first);
    fputs("\n  }\n}\n", out);

    (void)buf;
}
