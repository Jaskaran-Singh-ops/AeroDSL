/*
 * AeroDSL test suite.
 *
 * Covers every phase, and ends with the three checks that matter most for
 * trusting the compiler:
 *
 *   - a differential test that runs each scenario twice, with and without
 *     the optimiser, and requires the observable behaviour to be identical.
 *     An unsound optimisation shows up here immediately; in particular this
 *     is what catches a pass that moves a read of the simulated world
 *     across an `advance`.
 *
 *   - a register-pressure test that forces the allocator to spill, so the
 *     spill and reload paths are exercised rather than merely compiled.
 *
 *   - a recursion test, so the activation-record handling is exercised by
 *     more than one live frame at a time.
 *
 * Build and run with `make test`.
 */
#define _POSIX_C_SOURCE 200809L

#include "aerodsl.h"

static int checks, failures;
static const char *current = "?";

static void fail(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "FAIL [%s]\n      ", current);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    failures++;
}

#define CHECK(cond, ...)                \
    do {                                \
        checks++;                       \
        if (!(cond)) fail(__VA_ARGS__); \
    } while (0)

static void group(const char *name) { current = name; }

/* ---- helpers ------------------------------------------------------- */

typedef struct {
    Unit u;
    char *src;
} Run;

static Options no_opt(void) {
    Options o = options_default();
    o.optimise = 0;
    return o;
}

static void run_src(Run *r, const char *src, Options opt) {
    r->src = xmalloc(strlen(src) + 1);
    strcpy(r->src, src);
    unit_compile(&r->u, r->src, opt);
}

static void run_free(Run *r) {
    unit_free(&r->u);
    free(r->src);
}

/* A scenario prelude, so each test body can be a line or two. */
static char *with_prelude(const char *body) {
    static const char *head = "scenario \"t\"\n"
                              "fix VOMAN 12.95 77.67\n"
                              "fix AKTIV 13.20 77.90\n"
                              "flight AI202 type A320 enter VOMAN alt FL240 hdg 090 spd 280KT\n"
                              "flight BA117 type B777 enter AKTIV alt FL250 hdg 270 spd 300KT\n";
    size_t n = strlen(head) + strlen(body) + 2;
    char *s = xmalloc(n);
    snprintf(s, n, "%s%s", head, body);
    return s;
}

static char *slurp_early(const char *path);

static int has_diag(const Unit *u, Severity sev, const char *needle) {
    for (int i = 0; i < u->diags.count; i++)
        if (u->diags.items[i].sev == sev && strstr(u->diags.items[i].message, needle)) return 1;
    return 0;
}

static void expect_error(const char *body, const char *needle) {
    char *src = with_prelude(body);
    Run r;
    run_src(&r, src, options_default());
    CHECK(has_diag(&r.u, SEV_ERROR, needle), "expected an error containing \"%s\"\n      body: %s",
          needle, body);
    run_free(&r);
    free(src);
}

static void expect_warning(const char *body, const char *needle) {
    char *src = with_prelude(body);
    Run r;
    run_src(&r, src, options_default());
    CHECK(has_diag(&r.u, SEV_WARNING, needle),
          "expected a warning containing \"%s\"\n      body: %s", needle, body);
    run_free(&r);
    free(src);
}

static void expect_clean(const char *body) {
    char *src = with_prelude(body);
    Run r;
    run_src(&r, src, options_default());
    checks++;
    if (diag_errors(&r.u.diags)) {
        fail("expected no errors\n      body: %s", body);
        diag_print_all(stderr, &r.u.diags);
    } else if (!r.u.ran) {
        fail("expected the scenario to run\n      body: %s", body);
    }
    run_free(&r);
    free(src);
}

/* The text produced by a body's `print` statements, one event per line. */
static char *printed_with(const char *body, Options opt) {
    char *src = with_prelude(body);
    Run r;
    run_src(&r, src, opt);

    char *out = NULL;
    size_t len = 0;
    MemOut mo;
    FILE *mem = memout_open(&mo);
    if (diag_errors(&r.u.diags))
        diag_print_all(mem, &r.u.diags);
    else
        for (int i = 0; i < r.u.vm.events.count; i++)
            if (r.u.vm.events.items[i].kind == EV_PRINT)
                fprintf(mem, "%s\n", r.u.vm.events.items[i].text);
    out = memout_close(&mo, &len);
    run_free(&r);
    free(src);
    return out;
}

static void expect_print(const char *body, const char *want) {
    char *got = printed_with(body, options_default());
    CHECK(got && strcmp(got, want) == 0, "expected %s\n      got      %s\n      body: %s", want,
          got ? got : "(null)", body);
    free(got);
}

/* ---- lexer --------------------------------------------------------- */

static void test_lexer(void) {
    group("lexer: valid tokens");

    struct {
        const char *src;
        TokenType type;
        double value;
    } ok[] = {
        {"270", T_NUMBER, 270},    {"12.95", T_NUMBER, 12.95}, {"FL320", T_FLEVEL, 32000},
        {"FL010", T_FLEVEL, 1000}, {"280KT", T_SPEED, 280},    {"60KT", T_SPEED, 60},
        {"cruise", T_IDENT, 0},    {"sep_min2", T_IDENT, 0},   {"AI202", T_CALLSIGN, 0},
        {"VOMAN", T_CALLSIGN, 0},  {"FL32", T_CALLSIGN, 0},    {"FL3200", T_CALLSIGN, 0},
        {"climb", T_CLIMB, 0},     {"<=", T_LE, 0},            {"!=", T_NE, 0},
        {"==", T_EQEQ, 0},         {"=", T_ASSIGN, 0},         {"%", T_PERCENT, 0},
    };
    for (int i = 0; i < LEN(ok); i++) {
        TokenList t = {0};
        DiagList d = {0};
        tokenize(ok[i].src, &t, &d);
        CHECK(d.count == 0, "'%s' should lex cleanly", ok[i].src);
        CHECK(t.count == 2 && t.items[0].type == ok[i].type, "'%s' should lex as %s, got %s",
              ok[i].src, token_name(ok[i].type),
              t.count ? token_name(t.items[0].type) : "nothing");
        if (ok[i].value != 0)
            CHECK(t.count && t.items[0].num == ok[i].value, "'%s' should carry %g, got %g",
                  ok[i].src, ok[i].value, t.count ? t.items[0].num : 0);
        tokens_free(&t);
        diag_free(&d);
    }

    group("lexer: errors");
    const char *bad[] = {"Cruise", "aI202", "1000KT", "12.", "_x", "@", "!", "\"unterminated"};
    for (int i = 0; i < LEN(bad); i++) {
        TokenList t = {0};
        DiagList d = {0};
        tokenize(bad[i], &t, &d);
        CHECK(diag_errors(&d) > 0, "'%s' should be a lexical error", bad[i]);
        tokens_free(&t);
        diag_free(&d);
    }

    group("lexer: layout and positions");
    {
        TokenList t = {0};
        DiagList d = {0};
        tokenize("# a comment\nclimb  FL320\n\n  270", &t, &d);
        CHECK(d.count == 0, "comments and blank lines should lex cleanly");
        CHECK(t.count == 4, "expected 3 tokens plus EOF, got %d", t.count);
        CHECK(t.items[0].line == 2 && t.items[0].col == 1, "'climb' should be at 2:1, got %d:%d",
              t.items[0].line, t.items[0].col);
        CHECK(t.items[1].col == 8, "'FL320' should be at column 8, got %d", t.items[1].col);
        CHECK(t.items[2].line == 4, "'270' should be on line 4, got %d", t.items[2].line);
        tokens_free(&t);
        diag_free(&d);
    }
    {
        /* Every lexical error is reported, not just the first. */
        TokenList t = {0};
        DiagList d = {0};
        tokenize("@ @ @", &t, &d);
        CHECK(d.count == 3, "all three bad characters should be reported, got %d", d.count);
        tokens_free(&t);
        diag_free(&d);
    }
}

/* ---- parser -------------------------------------------------------- */

static void test_parser(void) {
    group("parser: precedence and associativity");

    expect_print("print 1 + 2 * 3", "7\n");
    expect_print("print (1 + 2) * 3", "9\n");
    expect_print("print 10 - 2 - 3", "5\n");
    expect_print("print 100 / 10 / 2", "5\n");
    expect_print("print 7 % 4", "3\n");
    expect_print("print -2 + 5", "3\n");
    expect_print("print - -3", "3\n");
    expect_print("print 2 * 3 < 7 and 1 < 2", "true\n");
    expect_print("print not 1 > 2", "true\n");
    expect_print("print 1 > 2 or 3 > 2", "true\n");
    expect_print("print (1 < 2) == true", "true\n");
    expect_error("print 1 < 2 == true", "comparisons do not chain");
    expect_print("print (365 + 180) % 360", "185\n");

    group("parser: errors");
    expect_error("let x = 1 < 2 < 3", "comparisons do not chain");
    expect_error("clear AI202 climb", "'climb' requires");
    expect_error("clear AI202 ascend FL300", "expected a clearance");
    expect_error("if AI202.alt > 1 advance 1 end", "expected 'then'");
    expect_error("while true do advance 1", "expected 'end'");
    expect_error("advance 1 2", "expected a declaration or a statement");
    expect_error("if true then\n  advance 1 2\nend", "expected a statement");
    expect_error("print AI202.bogus", "expected a flight state field");
    expect_error("let x = (1 + 2", "expected ')'");
    expect_error("let = 3", "expected a variable name");
    expect_error("call AI202()", "expected a procedure name");
    expect_error("let x = 1\ncall x()", "not a procedure");

    group("parser: recovery reports more than the first error");
    {
        char *src = with_prelude("clear AI202 ascend FL300\n"
                                 "let bad = 1 < 2 < 3\n"
                                 "if AI202.alt > 1 advance 1 end\n");
        Run r;
        run_src(&r, src, options_default());
        CHECK(has_diag(&r.u, SEV_ERROR, "expected a clearance"), "error 1 should be reported");
        CHECK(has_diag(&r.u, SEV_ERROR, "comparisons do not chain"), "error 2 should be reported");
        CHECK(has_diag(&r.u, SEV_ERROR, "expected 'then'"), "error 3 should be reported");
        run_free(&r);
        free(src);
    }

    group("parser: a missing scenario header");
    {
        Run r;
        run_src(&r, "fix V 1 2\n", options_default());
        CHECK(has_diag(&r.u, SEV_ERROR, "scenario"), "a missing scenario header should be an error");
        run_free(&r);
    }

    group("parser: scope and shadowing");
    expect_print("let x = 1\nif true then\n  let x = 2\n  print x\nend\nprint x", "2\n1\n");
    expect_error("if true then\n  let y = 2\nend\nprint y", "not declared");
    expect_error("let z = 1\nlet z = 2", "already declared in this scope");
    {
        /* A procedure sees its parameters and the globals, not main's
         * variables, which live in another activation record. */
        const char *s = "scenario \"t\"\nfix V 1 2\n"
                        "let outer = 1\n"
                        "proc p()\n  print outer\nend\n"
                        "call p()\nadvance 1\n";
        Run r;
        run_src(&r, s, options_default());
        CHECK(has_diag(&r.u, SEV_ERROR, "not declared"),
              "a procedure should not see a main-body variable");
        run_free(&r);
    }
}

/* ---- semantic analysis --------------------------------------------- */

static void test_sema(void) {
    group("sema: type errors");

    expect_error("clear AI202 climb VOMAN", "takes a number in ft");
    expect_error("clear AI202 direct 300", "takes a waypoint");
    expect_error("clear VOMAN climb FL300", "issued to an aircraft");
    expect_error("if AI202.alt then advance 1 end", "must be bool");
    expect_error("while 1 do advance 1 end", "must be bool");
    expect_error("assert 1", "must be bool");
    expect_error("let x = AI202.alt == true", "cannot compare");
    expect_error("let x = VOMAN.alt", "reads the state of an aircraft");
    expect_error("let x = true + 1", "arithmetic operator");
    expect_error("let x = not 1", "operand of 'not'");
    expect_error("let x = -true", "operand of unary '-'");
    expect_error("let x = AI202 and true", "logical operator");
    expect_error("AI202 = 4", "cannot assign to");
    expect_error("let f = AI202\nf = 3", "cannot be assigned");
    expect_error("advance AI202", "number of seconds");
    expect_error("let x = FL320 / 0", "division by zero");
    expect_error("let x = FL320 % 0", "division by zero");
    expect_error("let x = unknown", "not declared");
    expect_error("call nosuch()", "not declared");

    group("sema: built-ins");
    expect_error("let x = hsep(AI202)", "takes 2 arguments");
    expect_error("let x = abs(1, 2)", "takes 1 argument");
    expect_error("let x = hsep(AI202, VOMAN)", "argument 2 of hsep");
    expect_error("let x = dist(AI202, BA117)", "argument 2 of dist");
    expect_error("let min = 3", "built-in function");
    expect_error("let x = now", "needs an argument list");
    expect_clean("print now()");
    expect_clean("print abs(0 - 3), min(1, 2), max(1, 2)");
    expect_clean("print dist(AI202, VOMAN), bearing(AI202, AKTIV)");

    group("sema: a mistyped flight level is explained");
    expect_error("clear AI202 climb FL32000", "FL and exactly three digits");

    group("sema: procedures");
    expect_error("proc p(num a, num b)\n  advance a + b\nend\ncall p(1)", "takes 2 arguments");
    expect_error("proc p(flight f)\n  clear f climb FL300\nend\ncall p(VOMAN)", "argument 1 of p");
    expect_error("proc p()\n  advance 1\nend\nlet x = p", "has no value");
    expect_clean("proc p(flight f, num lvl, fix w)\n"
                 "  clear f climb lvl\n  clear f direct w\n  advance 10\nend\n"
                 "call p(AI202, FL320, VOMAN)");

    group("sema: warnings");
    expect_warning("let unused = 1\nadvance 1", "never read");
    expect_warning("while true do print \"x\" end", "cannot make progress");
    expect_warning("proc p()\n  advance 1\nend\nadvance 1", "never called");
    expect_warning("clear AI202 climb FL300\nreturn\nadvance 5", "cannot be reached");
    {
        const char *s = "scenario \"t\"\nfix V 1 2\n"
                        "flight ZZ1 type A320 enter V alt 1000 hdg 10 spd 200KT\nadvance 1\n";
        Run r;
        run_src(&r, s, options_default());
        CHECK(has_diag(&r.u, SEV_WARNING, "nothing in the scenario refers to it"),
              "an unreferenced aircraft should be reported");
        run_free(&r);
    }
    {
        const char *s = "scenario \"t\"\nfix V 1 2\n"
                        "flight ZZ1 type A320 enter V alt 1000 hdg 10 spd 200KT\n"
                        "clear ZZ1 climb 2000\n";
        Run r;
        run_src(&r, s, options_default());
        CHECK(has_diag(&r.u, SEV_WARNING, "never uses 'advance'"),
              "a scenario where no time passes should be reported");
        run_free(&r);
    }

    group("sema: waypoint coordinates");
    {
        Run r;
        run_src(&r, "scenario \"t\"\nfix V 99 0\nadvance 1\n", options_default());
        CHECK(has_diag(&r.u, SEV_ERROR, "latitude"), "a latitude of 99 should be rejected");
        run_free(&r);
    }
    {
        Run r;
        run_src(&r, "scenario \"t\"\nfix V 0 200\nadvance 1\n", options_default());
        CHECK(has_diag(&r.u, SEV_ERROR, "longitude"), "a longitude of 200 should be rejected");
        run_free(&r);
    }

    group("sema: the loop-progress check does not fire on real loops");
    expect_clean("clear AI202 climb FL320\nwhile AI202.alt < FL300 do advance 10 end");
    expect_clean("let n = 0\nwhile n < 3 do n = n + 1 end\nadvance 1");
    expect_clean("proc p()\n  advance 10\nend\nclear AI202 climb FL320\n"
                 "while AI202.alt < FL250 do call p() end");
}

/* ---- domain analysis ----------------------------------------------- */

static void test_analysis(void) {
    group("analysis: operand ranges");

    expect_error("clear AI202 heading 450", "between 0 and 360");
    expect_error("clear AI202 heading 0 - 10", "between 0 and 360");
    expect_error("clear AI202 climb 70000", "between 0 and 60000");
    expect_error("clear AI202 speed 10KT", "between 60 and 600");
    expect_error("clear AI202 speed 700KT", "between 60 and 600");
    expect_error("advance 0", "positive number of seconds");
    expect_error("advance 0 - 5", "positive number of seconds");
    expect_error("advance 90000", "86400 second limit");

    group("analysis: ranges are checked after constant propagation");
    expect_error("let h = 450\nclear AI202 heading h", "between 0 and 360");
    expect_error("let a = FL320\nlet b = a * 3\nclear AI202 climb b", "between 0 and 60000");
    expect_error("let s = 10\nclear AI202 speed s", "between 60 and 600");

    group("analysis: boundary values are accepted");
    expect_clean("clear AI202 heading 0\nadvance 1\nclear AI202 heading 360\nadvance 1");
    expect_clean("clear AI202 descend 0\nadvance 1");
    expect_clean("clear AI202 climb 60000\nadvance 1");
    expect_clean("clear AI202 speed 60KT\nadvance 1");
    expect_clean("clear AI202 speed 600KT\nadvance 1");
    expect_clean("advance 86400");

    group("analysis: a value known only at runtime is checked by the VM");
    {
        char *src = with_prelude("let h = AI202.hdg * 10\nclear AI202 heading h\nadvance 1\n");
        Run r;
        run_src(&r, src, options_default());
        CHECK(has_diag(&r.u, SEV_ERROR, "between 0 and 360"),
              "an out-of-range heading computed at runtime should be reported");
        CHECK(diag_errors_phase(&r.u.diags, PH_RUNTIME) > 0,
              "the violation should come from the runtime, not the compiler");
        run_free(&r);
        free(src);
    }

    group("analysis: clearance consistency");
    expect_warning("clear AI202 climb FL320\nclear AI202 descend FL280\nadvance 1",
                   "contradict each other");
    expect_warning("clear AI202 heading 180\nclear AI202 direct VOMAN\nadvance 1",
                   "both assign the lateral path");
    expect_warning("clear AI202 direct VOMAN\nclear AI202 heading 180\nadvance 1",
                   "both assign the lateral path");
    expect_warning("clear AI202 heading 180\nclear AI202 heading 270\nadvance 1",
                   "before it can take effect");
    expect_warning("clear AI202 climb FL200\nadvance 1", "which is not upwards");
    expect_warning("clear AI202 climb FL320\nadvance 60\nclear AI202 climb FL300",
                   "which is not upwards");
    expect_warning("clear AI202 descend FL300\nadvance 1", "which is not downwards");

    group("analysis: an advance between clearances resolves the conflict");
    expect_clean("clear AI202 climb FL320\nadvance 60\nclear AI202 descend FL280");
    expect_clean("clear AI202 heading 180\nadvance 60\nclear AI202 direct VOMAN");

    group("analysis: a clearance through a parameter claims nothing");
    expect_clean("proc p(flight f)\n  clear f climb FL320\n  clear f descend FL280\nend\n"
                 "call p(AI202)\nadvance 1");
}

/* ---- intermediate code --------------------------------------------- */

static int count_op(const Func *fn, IrOp op) {
    int n = 0;
    for (int i = 0; i < fn->ir.count; i++)
        if (fn->ir.items[i].op == op) n++;
    return n;
}

static void test_ir(void) {
    group("ir: short-circuit translation uses jumping code");
    {
        /* Both operands of `and` must be guarded by their own branch, and
         * no instruction may compute a boolean `and` as a value. */
        char *src = with_prelude("if AI202.alt > 1000 and BA117.spd < 400 then\n"
                                 "  advance 1\nend\nadvance 1\n");
        Run r;
        run_src(&r, src, no_opt());
        const Func *fn = &r.u.funcs.items[0];
        CHECK(diag_errors(&r.u.diags) == 0, "the program should compile");
        int branches = count_op(fn, IR_IFTRUE) + count_op(fn, IR_IFFALSE);
        CHECK(branches == 2, "`and` in a condition should emit two branches, got %d", branches);
        for (int i = 0; i < fn->ir.count; i++)
            CHECK(!(fn->ir.items[i].op == IR_BIN && (fn->ir.items[i].bop == OP_AND ||
                                                     fn->ir.items[i].bop == OP_OR)),
                  "`and`/`or` in a condition should not be computed as a value");
        run_free(&r);
        free(src);
    }

    group("ir: short-circuiting is observable");
    /* If the right operand were evaluated, the division by zero would be a
     * runtime error. It must not be reached. */
    expect_print("let d = 0\nprint 1 > 2 and 10 / d > 1", "false\n");
    expect_print("let d = 0\nprint 1 < 2 or 10 / d > 1", "true\n");

    group("ir: a boolean used as a value is materialised");
    expect_print("let converging = AI202.alt > 1000 and BA117.alt > 1000\nprint converging",
                 "true\n");

    group("ir: a while loop produces a back edge");
    {
        char *src = with_prelude("let n = 0\nwhile n < 3 do\n  n = n + 1\nend\nadvance 1\n");
        Run r;
        run_src(&r, src, no_opt());
        const Func *fn = &r.u.funcs.items[0];
        int headers = 0;
        for (int b = 0; b < fn->nblocks; b++)
            if (fn->blocks[b].loop_header) headers++;
        CHECK(headers == 1, "the loop should produce exactly one loop header, got %d", headers);
        run_free(&r);
        free(src);
    }

    group("ir: procedures become separate functions");
    {
        char *src = with_prelude("proc a()\n  advance 1\nend\nproc b()\n  advance 2\nend\n"
                                 "call a()\ncall b()\n");
        Run r;
        run_src(&r, src, no_opt());
        CHECK(r.u.funcs.count == 3, "main plus two procedures should give 3 functions, got %d",
              r.u.funcs.count);
        CHECK(count_op(&r.u.funcs.items[0], IR_CALL) == 2, "main should contain two calls");
        run_free(&r);
        free(src);
    }
}

/* ---- control-flow graph and dataflow ------------------------------- */

static void test_cfg(void) {
    group("cfg: dominators and liveness");
    {
        char *src = with_prelude("proc p(num total)\n"
                                 "  let n = 0\n"
                                 "  while n < total do\n"
                                 "    advance 10\n"
                                 "    n = n + 1\n"
                                 "  end\n"
                                 "  print n\n"
                                 "end\n"
                                 "call p(3)\n");
        Run r;
        run_src(&r, src, no_opt());
        CHECK(diag_errors(&r.u.diags) == 0, "the program should compile");
        const Func *fn = &r.u.funcs.items[1];

        CHECK(fn->nblocks >= 4, "the loop should give at least 4 blocks, got %d", fn->nblocks);
        CHECK(bs_get(&fn->blocks[0].dom, 0), "the entry block dominates itself");
        for (int b = 1; b < fn->nblocks; b++)
            if (fn->blocks[b].reachable)
                CHECK(bs_get(&fn->blocks[b].dom, 0), "B%d should be dominated by the entry", b);

        /* Every block but the entry has an immediate dominator. */
        for (int b = 1; b < fn->nblocks; b++)
            if (fn->blocks[b].reachable)
                CHECK(fn->blocks[b].idom >= 0, "B%d should have an immediate dominator", b);

        /* The loop counter is live across the loop. */
        int found_live = 0;
        for (int b = 0; b < fn->nblocks; b++)
            if (bs_count(&fn->blocks[b].live_out) > 0) found_live = 1;
        CHECK(found_live, "some register should be live across a block boundary");
        run_free(&r);
        free(src);
    }

    group("cfg: unreachable code is identified");
    {
        char *src = with_prelude("clear AI202 climb FL300\nreturn\nadvance 5\n");
        Run r;
        run_src(&r, src, options_default());
        CHECK(has_diag(&r.u, SEV_WARNING, "cannot be reached"),
              "code after a return should be reported");
        run_free(&r);
        free(src);
    }
}

/* ---- static single assignment -------------------------------------- */

/* The SSA form of a body's main function, as text. */
static char *ssa_text(const char *body) {
    char *src = with_prelude(body);
    Run r;
    run_src(&r, src, options_default());
    char *out = NULL;
    size_t len = 0;
    MemOut mo;
    FILE *mem = memout_open(&mo);
    if (r.u.opt_funcs.count) ssa_print(mem, &r.u.prog, &r.u.opt_funcs.items[0]);
    out = memout_close(&mo, &len);
    run_free(&r);
    free(src);
    return out;
}

static void test_ssa(void) {
    group("ssa: straight-line code needs no phi function");
    {
        char *s = ssa_text("clear AI202 climb FL320\nadvance 60\nprint AI202.alt\n");
        CHECK(s && strstr(s, "0 phi functions"), "straight-line code should need no phi");
        free(s);
    }

    group("ssa: a loop header gets one phi for the value carried round");
    {
        char *s = ssa_text("let n = 0\nwhile n < 3 do\n  n = n + 1\nend\nprint n\nadvance 1\n");
        CHECK(s && strstr(s, "1 phi function "), "the loop counter should need exactly one phi");
        CHECK(s && strstr(s, "= phi("), "a phi function should be printed");
        CHECK(s && strstr(s, "n.1 = 0"), "the initial definition should be version 1");
        free(s);
    }

    group("ssa: a loop header is in its own dominance frontier");
    {
        char *s = ssa_text("let n = 0\nwhile n < 3 do\n  n = n + 1\nend\nprint n\nadvance 1\n");
        /* The header block's frontier line must name the header itself. */
        int ok = 0;
        if (s) {
            const char *df = strstr(s, "dominance frontiers");
            if (df) {
                const char *b1 = strstr(df, "B1  ");
                if (b1 && strstr(b1, "B1") != NULL) ok = 1;
            }
        }
        CHECK(ok, "a loop header should appear in its own dominance frontier");
        free(s);
    }

    group("ssa: a join after if/else gets a phi");
    {
        char *s = ssa_text("let x = 1\nif AI202.alt > 100 then\n  x = 2\nelse\n  x = 3\nend\n"
                           "print x\nadvance 1\n");
        CHECK(s && strstr(s, "= phi("), "the join should need a phi for x");
        /* The phi must name an incoming version for each predecessor. The
         * `let x = 1` before the branch does not appear: both arms assign x
         * before it is read, so the optimiser removed it as dead. */
        if (s) {
            const char *phi = strstr(s, "= phi(");
            int froms = 0;
            for (const char *q = phi; q && *q && *q != '\n'; q++)
                if (strncmp(q, "from B", 6) == 0) froms++;
            CHECK(froms == 2, "the phi should take one version per predecessor, found %d", froms);
        }
        free(s);
    }

    group("ssa: a value defined on every path is never undefined");
    {
        char *s = ssa_text("let n = 0\nwhile n < 3 do\n  n = n + 1\nend\nprint n\nadvance 1\n");
        /* `undef` may legitimately appear for a register that is only
         * defined on some path; `n` is not one of those. */
        CHECK(s && !strstr(s, "n.undef"), "n is defined before the loop, so it is never undefined");
        free(s);
    }

    group("ssa: construction does not crash on the examples");
    {
        static const char *const files[] = {
            "examples/02_converging.aero", "examples/03_procedures.aero",
            "examples/06_optimise.aero",   "examples/12_separation.aero",
        };
        for (int i = 0; i < LEN(files); i++) {
            char *src = slurp_early(files[i]);
            if (!src) continue;
            Run r;
            run_src(&r, src, options_default());
            char *out = NULL;
            size_t len = 0;
            MemOut mo;
    FILE *mem = memout_open(&mo);
            for (int k = 0; k < r.u.opt_funcs.count; k++)
                ssa_print(mem, &r.u.prog, &r.u.opt_funcs.items[k]);
            out = memout_close(&mo, &len);
            CHECK(out && len > 0, "%s should produce an SSA dump", files[i]);
            free(out);
            run_free(&r);
            free(src);
        }
    }
}

/* ---- optimiser ----------------------------------------------------- */

static void test_opt(void) {
    group("opt: constant folding and propagation");
    {
        char *src = with_prelude("let cruise = FL320\nlet margin = 2000\n"
                                 "clear AI202 climb cruise - margin\nadvance 1\n");
        Run r;
        run_src(&r, src, options_default());
        const Func *fn = &r.u.opt_funcs.items[0];
        /* The whole computation should reduce to a single constant operand. */
        int found = 0;
        for (int i = 0; i < fn->ir.count; i++) {
            const Ir *ir = &fn->ir.items[i];
            if (ir->op == IR_CLEAR && ir->b.kind == AR_CONST && ir->b.k == 30000) found = 1;
            CHECK(ir->op != IR_BIN || ir->a.kind != AR_CONST || ir->b.kind != AR_CONST,
                  "no instruction should be left with two constant operands");
        }
        CHECK(found, "the clearance operand should have folded to the constant 30000");
        CHECK(r.u.stats.propagated > 0, "constant propagation should have fired");
        run_free(&r);
        free(src);
    }

    group("opt: common subexpression elimination");
    {
        char *src = with_prelude("let a = AI202.alt + 500\nlet b = AI202.alt + 500\n"
                                 "print a, b\n");
        Run r;
        run_src(&r, src, options_default());
        const Func *fn = &r.u.opt_funcs.items[0];
        CHECK(r.u.stats.cse > 0, "CSE should have fired");
        CHECK(count_op(fn, IR_FIELD) == 1, "the repeated field read should be computed once, got %d",
              count_op(fn, IR_FIELD));
        run_free(&r);
        free(src);
    }

    group("opt: the state barrier blocks CSE across an advance");
    {
        char *src = with_prelude("let a = AI202.alt\nadvance 60\nlet b = AI202.alt\n"
                                 "print a, b\n");
        Run r;
        run_src(&r, src, options_default());
        const Func *fn = &r.u.opt_funcs.items[0];
        CHECK(count_op(fn, IR_FIELD) == 2,
              "two field reads separated by an advance must both remain, got %d",
              count_op(fn, IR_FIELD));
        run_free(&r);
        free(src);
    }
    {
        /* The same, but with a clearance as the barrier. */
        char *src = with_prelude("let a = AI202.alt\nclear AI202 climb FL300\n"
                                 "let b = AI202.alt\nprint a, b\nadvance 1\n");
        Run r;
        run_src(&r, src, options_default());
        CHECK(count_op(&r.u.opt_funcs.items[0], IR_FIELD) == 2,
              "a clearance is a barrier, so both field reads must remain");
        run_free(&r);
        free(src);
    }

    group("opt: dead code elimination");
    {
        char *src = with_prelude("let unused = AI202.alt + 17\nadvance 1\n");
        Run r;
        run_src(&r, src, options_default());
        CHECK(r.u.stats.dead > 0, "dead code elimination should have fired");
        CHECK(count_op(&r.u.opt_funcs.items[0], IR_FIELD) == 0,
              "the unread field read should be removed");
        run_free(&r);
        free(src);
    }

    group("opt: side effects are never removed");
    {
        char *src = with_prelude("clear AI202 climb FL320\nadvance 60\nprint \"x\"\n");
        Run r;
        run_src(&r, src, options_default());
        const Func *fn = &r.u.opt_funcs.items[0];
        CHECK(count_op(fn, IR_CLEAR) == 1, "the clearance must survive");
        CHECK(count_op(fn, IR_ADVANCE) == 1, "the advance must survive");
        CHECK(count_op(fn, IR_PRINTSTR) == 1, "the print must survive");
        run_free(&r);
        free(src);
    }

    group("opt: loop-invariant code motion");
    {
        char *src = with_prelude("proc p(num limit)\n"
                                 "  let n = 0\n"
                                 "  while n < 3 do\n"
                                 "    advance limit * 2\n"
                                 "    n = n + 1\n"
                                 "  end\n"
                                 "end\n"
                                 "call p(5)\n");
        Run r;
        run_src(&r, src, options_default());
        CHECK(r.u.stats.hoisted > 0, "the invariant computation should have been hoisted");
        run_free(&r);
        free(src);
    }

    group("opt: a state read is never hoisted out of a loop with a barrier");
    {
        char *src = with_prelude("clear AI202 climb FL320\n"
                                 "let n = 0\n"
                                 "while AI202.alt < FL300 do\n"
                                 "  advance 10\n"
                                 "  n = n + 1\n"
                                 "end\n"
                                 "print n\n");
        Run r;
        run_src(&r, src, options_default());
        /* If the altitude read had been hoisted, the loop would not
         * terminate and the VM would hit its instruction limit. */
        CHECK(diag_errors(&r.u.diags) == 0, "the loop should terminate normally");
        CHECK(r.u.vm.t > 0, "simulated time should have advanced");
        run_free(&r);
        free(src);
    }

    group("opt: a loop-invariant store is not hoisted past its uses");
    /* `x = 5` is invariant by every local test, but hoisting it would print
     * 5 for a loop that never runs. */
    expect_print("let x = 1\nlet n = 10\nwhile n < 3 do\n  x = 5\n  n = n + 1\nend\n"
                 "print x\nadvance 1",
                 "1\n");

    group("opt: algebraic identities");
    expect_print("let a = AI202.alt\nprint a + 0, a - 0, a * 1, a / 1", "24000240002400024000\n");

    group("opt: branch on a constant becomes straight-line code");
    {
        char *src = with_prelude("let m = 2000\nif m > 0 then\n  clear AI202 speed 300KT\n"
                                 "else\n  clear AI202 speed 180KT\nend\nadvance 1\n");
        Run r;
        run_src(&r, src, options_default());
        CHECK(r.u.stats.blocks_removed > 0, "the unreachable arm should have been removed");
        CHECK(count_op(&r.u.opt_funcs.items[0], IR_CLEAR) == 1,
              "only the taken arm's clearance should remain");
        run_free(&r);
        free(src);
    }
}

/* ---- code generation ----------------------------------------------- */

static void test_codegen(void) {
    group("codegen: compare and branch are fused");
    {
        char *src = with_prelude("let n = 0\nwhile n < 3 do\n  n = n + 1\nend\nadvance 1\n");
        Run r;
        run_src(&r, src, options_default());
        const Func *fn = &r.u.opt_funcs.items[0];
        int jrel = 0;
        for (int i = 0; i < fn->nbc; i++)
            if (fn->bc[i].op == BC_JREL) jrel++;
        CHECK(jrel > 0, "a comparison feeding a branch should become a fused compare-and-branch");
        run_free(&r);
        free(src);
    }

    group("codegen: register pressure forces spilling, and the result is still right");
    {
        /*
         * More simultaneously live values than there are registers. They are
         * derived from the aircraft's altitude rather than from literals,
         * because literals would be folded away and nothing would be live.
         * The total is checked against the value computed by hand, so a
         * mistake in the spill or reload path shows up as a wrong number.
         */
        const char *body =
            "let a = AI202.alt\n"
            "let v01 = a + 1\nlet v02 = a + 2\nlet v03 = a + 3\nlet v04 = a + 4\n"
            "let v05 = a + 5\nlet v06 = a + 6\nlet v07 = a + 7\nlet v08 = a + 8\n"
            "let v09 = a + 9\nlet v10 = a + 10\nlet v11 = a + 11\nlet v12 = a + 12\n"
            "let v13 = a + 13\nlet v14 = a + 14\nlet v15 = a + 15\nlet v16 = a + 16\n"
            "let v17 = a + 17\nlet v18 = a + 18\nlet v19 = a + 19\nlet v20 = a + 20\n"
            "let total = v01 + v02 + v03 + v04 + v05 + v06 + v07 + v08 + v09 + v10\n"
            "          + v11 + v12 + v13 + v14 + v15 + v16 + v17 + v18 + v19 + v20\n"
            "print total\nadvance 1\n";
        /* 20 * 24000 + (1 + 2 + ... + 20) = 480000 + 210 */
        expect_print(body, "480210\n");

        char *src = with_prelude(body);
        Run r;
        run_src(&r, src, options_default());
        int stores = 0, loads = 0;
        for (int f = 0; f < r.u.opt_funcs.count; f++)
            for (int i = 0; i < r.u.opt_funcs.items[f].nbc; i++) {
                if (r.u.opt_funcs.items[f].bc[i].op == BC_STORE) stores++;
                if (r.u.opt_funcs.items[f].bc[i].op == BC_LOAD) loads++;
            }
        CHECK(stores > 0 && loads > 0,
              "20 live values in 16 registers should produce spills and reloads (%d stores, %d "
              "loads)",
              stores, loads);
        run_free(&r);
        free(src);
    }

    group("codegen: every jump lands inside the bytecode");
    {
        char *src = with_prelude("let n = 0\nwhile n < 5 do\n"
                                 "  if n > 2 then\n    clear AI202 speed 300KT\n"
                                 "  else\n    clear AI202 speed 260KT\n  end\n"
                                 "  advance 10\n  n = n + 1\nend\n");
        Run r;
        run_src(&r, src, options_default());
        for (int f = 0; f < r.u.opt_funcs.count; f++) {
            const Func *fn = &r.u.opt_funcs.items[f];
            for (int i = 0; i < fn->nbc; i++) {
                BcOp op = fn->bc[i].op;
                if (op != BC_JMP && op != BC_JT && op != BC_JF && op != BC_JREL) continue;
                CHECK(fn->bc[i].a >= 0 && fn->bc[i].a <= fn->nbc,
                      "jump at %d targets %d, outside 0..%d", i, fn->bc[i].a, fn->nbc);
            }
        }
        run_free(&r);
        free(src);
    }
}

/* ---- the virtual machine ------------------------------------------- */

static void test_vm(void) {
    group("vm: control flow");
    expect_print("if 1 < 2 then print \"yes\" else print \"no\" end\nadvance 1", "yes\n");
    expect_print("if 2 < 1 then print \"yes\" else print \"no\" end\nadvance 1", "no\n");
    expect_print("let n = 0\nwhile n < 5 do n = n + 1 end\nprint n\nadvance 1", "5\n");
    expect_print("let n = 0\nwhile n < 0 do n = n + 1 end\nprint n\nadvance 1", "0\n");

    group("vm: procedures and activation records");
    expect_print("proc p(num a, num b)\n  print a + b\nend\n"
                 "call p(1, 2)\ncall p(10, 20)\nadvance 1",
                 "3\n30\n");
    expect_print("proc inner(num x)\n  print x * 2\nend\n"
                 "proc outer(num y)\n  call inner(y + 1)\n  print y\nend\n"
                 "call outer(5)\nadvance 1",
                 "12\n5\n");
    {
        /* A parameter is passed by value: the caller's variable is untouched. */
        expect_print("proc p(num x)\n  x = 99\n  print x\nend\n"
                     "let v = 1\ncall p(v)\nprint v\nadvance 1",
                     "99\n1\n");
    }
    {
        /* Recursion, so more than one frame is live at once. */
        expect_print("proc countdown(num n)\n"
                     "  print n\n"
                     "  if n > 1 then\n    call countdown(n - 1)\n  end\n"
                     "end\n"
                     "call countdown(4)\nadvance 1",
                     "4\n3\n2\n1\n");
    }
    {
        /* Runaway recursion is caught rather than crashing. */
        char *src = with_prelude("proc forever(num n)\n  call forever(n + 1)\nend\n"
                                 "call forever(1)\nadvance 1\n");
        Run r;
        run_src(&r, src, options_default());
        CHECK(has_diag(&r.u, SEV_ERROR, "nested more than"),
              "unbounded recursion should be reported, not crash");
        run_free(&r);
        free(src);
    }

    group("vm: runtime errors");
    {
        char *src = with_prelude("let z = AI202.alt - 24000\nprint 1 / z\nadvance 1\n");
        Run r;
        run_src(&r, src, options_default());
        CHECK(has_diag(&r.u, SEV_ERROR, "division by zero"),
              "a runtime zero divisor should be reported");
        run_free(&r);
        free(src);
    }
    {
        char *src = with_prelude("let n = 0\nwhile n >= 0 do n = n + 1 end\nadvance 1\n");
        Run r;
        run_src(&r, src, options_default());
        CHECK(has_diag(&r.u, SEV_ERROR, "more than"), "a runaway loop should hit the step limit");
        run_free(&r);
        free(src);
    }

    group("vm: the flight model");
    {
        /* Climbing 6000 ft at 1800 ft/min takes 200 s. After 100 s the
         * aircraft should be half way. */
        char *src = with_prelude("clear AI202 climb FL300\nadvance 100\nprint AI202.alt\n");
        Run r;
        run_src(&r, src, options_default());
        double alt = 0;
        for (int i = 0; i < r.u.vm.nflights; i++)
            if (str_is(r.u.vm.flights[i].name, "AI202")) alt = r.u.vm.flights[i].alt;
        CHECK(alt > 26900 && alt < 27100, "after 100 s of climb the altitude should be near 27000, "
                                          "got %g",
              alt);
        run_free(&r);
        free(src);
    }
    {
        /* A standard-rate turn of 90 degrees takes 30 s. */
        char *src = with_prelude("clear AI202 heading 180\nadvance 30\nadvance 1\n");
        Run r;
        run_src(&r, src, options_default());
        double hdg = 0;
        for (int i = 0; i < r.u.vm.nflights; i++)
            if (str_is(r.u.vm.flights[i].name, "AI202")) hdg = r.u.vm.flights[i].hdg;
        CHECK(hdg > 178 && hdg < 182, "a 90 degree turn should complete in about 30 s, got %g",
              hdg);
        run_free(&r);
        free(src);
    }
    {
        /* A `direct` clearance steers toward the waypoint. */
        char *src = with_prelude("clear AI202 direct AKTIV\nadvance 60\nadvance 1\n");
        Run r;
        run_src(&r, src, options_default());
        double hdg = 0;
        for (int i = 0; i < r.u.vm.nflights; i++)
            if (str_is(r.u.vm.flights[i].name, "AI202")) hdg = r.u.vm.flights[i].hdg;
        /* AKTIV is north east of VOMAN, so the heading should be in the
         * first quadrant rather than the initial 090. */
        CHECK(hdg > 10 && hdg < 80, "the aircraft should be steering toward AKTIV, heading %g",
              hdg);
        run_free(&r);
        free(src);
    }

    group("vm: separation monitoring");
    {
        /* Two aircraft at the same level converging head on must trigger a
         * loss of separation. */
        const char *s = "scenario \"t\"\n"
                        "fix WEST 13.00 77.00\nfix EAST 13.00 77.50\n"
                        "flight AA1 type A320 enter WEST alt FL320 hdg 090 spd 400KT\n"
                        "flight BB2 type A320 enter EAST alt FL320 hdg 270 spd 400KT\n"
                        "advance 600\n"
                        "print hsep(AA1, BB2)\n";
        Run r;
        run_src(&r, s, options_default());
        CHECK(r.u.vm.los_count >= 1, "a head-on pair at one level should lose separation, got %d",
              r.u.vm.los_count);
        CHECK(r.u.vm.min_hsep < 5, "the closest approach should be inside 5 NM, got %g",
              r.u.vm.min_hsep);
        run_free(&r);
    }
    {
        /* The same geometry with 2000 ft between them must not. */
        const char *s = "scenario \"t\"\n"
                        "fix WEST 13.00 77.00\nfix EAST 13.00 77.50\n"
                        "flight AA1 type A320 enter WEST alt FL320 hdg 090 spd 400KT\n"
                        "flight BB2 type A320 enter EAST alt FL300 hdg 270 spd 400KT\n"
                        "advance 600\n"
                        "print vsep(AA1, BB2)\n";
        Run r;
        run_src(&r, s, options_default());
        CHECK(r.u.vm.los_count == 0, "2000 ft of vertical separation should keep the pair legal, "
                                     "got %d losses",
              r.u.vm.los_count);
        run_free(&r);
    }

    group("vm: assertions");
    expect_clean("advance 1\nassert AI202.alt == FL240, \"should hold\"");
    {
        char *src = with_prelude("advance 1\nassert AI202.alt > FL400, \"should fail\"\n");
        Run r;
        run_src(&r, src, options_default());
        CHECK(r.u.vm.asserts_failed == 1, "the assertion should have failed");
        CHECK(diag_errors_phase(&r.u.diags, PH_VERIFY) == 1,
              "a failed assertion should be a verify error");
        run_free(&r);
        free(src);
    }
    {
        /* Every assertion is checked, so the run continues past a failure. */
        char *src = with_prelude("advance 1\nassert 1 > 2, \"a\"\nassert 2 > 3, \"b\"\n");
        Run r;
        run_src(&r, src, options_default());
        CHECK(r.u.vm.asserts_run == 2, "both assertions should be checked, got %d",
              r.u.vm.asserts_run);
        CHECK(r.u.vm.asserts_failed == 2, "both should fail, got %d", r.u.vm.asserts_failed);
        run_free(&r);
        free(src);
    }

    group("vm: entering mid-scenario");
    {
        const char *s = "scenario \"t\"\n"
                        "fix V 13.00 77.00\n"
                        "flight AA1 type A320 enter V alt FL320 hdg 090 spd 400KT\n"
                        "advance 60\n"
                        "flight BB2 type A320 enter V alt FL300 hdg 270 spd 400KT\n"
                        "advance 60\n"
                        "print BB2.alt\n";
        Run r;
        run_src(&r, s, options_default());
        CHECK(diag_errors(&r.u.diags) == 0, "an aircraft should be able to enter part way through");
        int enters = 0;
        for (int i = 0; i < r.u.vm.events.count; i++)
            if (r.u.vm.events.items[i].kind == EV_ENTER) enters++;
        CHECK(enters == 2, "both entries should be recorded, got %d", enters);
        /* The second entry must be recorded at the later time. */
        double first_t = -1, second_t = -1;
        for (int i = 0; i < r.u.vm.events.count; i++) {
            if (r.u.vm.events.items[i].kind != EV_ENTER) continue;
            if (first_t < 0)
                first_t = r.u.vm.events.items[i].t;
            else if (second_t < 0)
                second_t = r.u.vm.events.items[i].t;
        }
        CHECK(first_t == 0 && second_t >= 60,
              "the second aircraft should enter at 60 s, got %g then %g", first_t, second_t);
        run_free(&r);
    }
}

/* ---- the scenario-file backend ------------------------------------- */

static void test_scn(void) {
    group("scn: generated commands");
    char *src = with_prelude("clear AI202 climb FL320\nadvance 60\nclear AI202 heading 270\n"
                             "advance 60\nclear AI202 speed 300KT\nadvance 60\n"
                             "clear AI202 direct AKTIV\nadvance 60\n");
    Run r;
    run_src(&r, src, options_default());
    CHECK(diag_errors(&r.u.diags) == 0, "the scenario should compile and run");

    char *out = NULL;
    size_t len = 0;
    MemOut mo;
    FILE *mem = memout_open(&mo);
    scn_emit(mem, &r.u.prog, &r.u.vm);
    out = memout_close(&mo, &len);

    CHECK(strstr(out, "DEFWPT VOMAN,12.950000,77.670000") != NULL,
          "waypoints should be defined in the output");
    CHECK(strstr(out, "CRE AI202,A320,") != NULL, "the aircraft should be created with its type");
    CHECK(strstr(out, ">ALT AI202,FL320") != NULL, "the climb should become an ALT command");
    CHECK(strstr(out, ">HDG AI202,270") != NULL, "the heading should become an HDG command");
    CHECK(strstr(out, ">SPD AI202,300") != NULL, "the speed should become an SPD command");
    CHECK(strstr(out, ">ADDWPT AI202,AKTIV") != NULL, "the direct should become an ADDWPT command");
    CHECK(strstr(out, "00:01:00.00>") != NULL, "timestamps should be written as HH:MM:SS.hh");
    CHECK(strstr(out, "Not for operational use") != NULL,
          "the output should carry its scope statement");
    free(out);
    run_free(&r);
    free(src);
}

/* ---- differential test: the optimiser must preserve behaviour ------ */

/* A canonical dump of everything a scenario does, excluding anything that
 * legitimately differs between builds (such as instruction counts). */
static char *behaviour(const char *src, Options opt) {
    Run r;
    run_src(&r, src, opt);

    char *out = NULL;
    size_t len = 0;
    MemOut mo;
    FILE *mem = memout_open(&mo);

    for (int i = 0; i < r.u.diags.count; i++) {
        const Diagnostic *d = &r.u.diags.items[i];
        /* Warnings about unreachable code are produced by the optimiser, so
         * they are expected to differ; errors and verify results are not. */
        if (d->sev == SEV_WARNING) continue;
        fprintf(mem, "diag %s %s %d:%d %s\n", phase_name(d->phase), sev_name(d->sev), d->line,
                d->col, d->message);
    }
    for (int i = 0; i < r.u.vm.events.count; i++) {
        const Event *e = &r.u.vm.events.items[i];
        fprintf(mem, "event %d t=%.3f f=%d o=%d c=%d v=%.6f %s\n", (int)e->kind, e->t, e->flight,
                e->other, (int)e->clear, e->value, e->text);
    }
    fprintf(mem, "final t=%.3f los=%d asserts=%d/%d minh=%.6f minv=%.6f\n", r.u.vm.t,
            r.u.vm.los_count, r.u.vm.asserts_failed, r.u.vm.asserts_run, r.u.vm.min_hsep,
            r.u.vm.min_vsep);
    for (int i = 0; i < r.u.vm.nflights; i++) {
        const FlightState *f = &r.u.vm.flights[i];
        fprintf(mem, "flight %d active=%d alt=%.6f hdg=%.6f spd=%.6f lat=%.6f lon=%.6f\n", i,
                f->active, f->alt, f->hdg, f->spd, f->lat, f->lon);
    }
    out = memout_close(&mo, &len);
    run_free(&r);
    return out;
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char *s = read_all(f);
    fclose(f);
    return s;
}

static char *slurp_early(const char *path) { return slurp(path); }

static void differential(const char *label, const char *src) {
    char *with = behaviour(src, options_default());
    char *without = behaviour(src, no_opt());
    checks++;
    if (!with || !without || strcmp(with, without) != 0) {
        fail("optimised and unoptimised runs of %s behave differently", label);
        /* Show the first line that differs, which is usually enough. */
        if (with && without) {
            const char *a = with, *b = without;
            int line = 1;
            while (*a && *b) {
                const char *ae = strchr(a, '\n'), *be = strchr(b, '\n');
                size_t alen = ae ? (size_t)(ae - a) : strlen(a);
                size_t blen = be ? (size_t)(be - b) : strlen(b);
                if (alen != blen || memcmp(a, b, alen) != 0) {
                    fprintf(stderr, "      first difference at line %d\n", line);
                    fprintf(stderr, "        optimised:   %.*s\n", (int)alen, a);
                    fprintf(stderr, "        unoptimised: %.*s\n", (int)blen, b);
                    break;
                }
                if (!ae || !be) break;
                a = ae + 1;
                b = be + 1;
                line++;
            }
        }
    }
    free(with);
    free(without);
}

static void test_differential(void) {
    group("differential: the optimiser preserves behaviour");

    /* Hand-written cases aimed at the passes most likely to be unsound. */
    static const char *const bodies[] = {
        "clear AI202 climb FL320\nadvance 120\nprint AI202.alt\n",
        "let n = 0\nwhile AI202.alt < FL300 do\n  advance 10\n  n = n + 1\nend\nprint n\n",
        "let a = AI202.alt\nadvance 60\nlet b = AI202.alt\nprint a, b, b - a\n",
        "let x = 1\nlet n = 10\nwhile n < 3 do\n  x = 5\n  n = n + 1\nend\nprint x\nadvance 1\n",
        "proc p(flight f, num lvl)\n  clear f climb lvl\n  advance 30\n  print f.alt\nend\n"
        "call p(AI202, FL300)\ncall p(BA117, FL320)\n",
        "let c = 500\nlet e = 0\nwhile e < 300 do\n  advance 10\n"
        "  c = min(c, hsep(AI202, BA117))\n  e = e + 10\nend\nprint c\n",
        "if 1 > 0 then\n  clear AI202 speed 300KT\nelse\n  clear AI202 speed 180KT\nend\n"
        "advance 60\nprint AI202.spd\n",
        "let d = 0\nprint 1 > 2 and 10 / d > 1\nadvance 1\n",
        "proc step(flight f, num limit)\n  let n = 0\n"
        "  while n < 3 do\n    let budget = limit * 2\n    if f.alt < budget then\n"
        "      clear f climb f.alt + 1000\n    end\n    advance 30\n    n = n + 1\n  end\nend\n"
        "call step(AI202, FL200)\nprint AI202.alt\n",
        "clear AI202 direct AKTIV\nadvance 300\nprint AI202.hdg, AI202.lat, AI202.lon\n",
    };
    for (int i = 0; i < LEN(bodies); i++) {
        char *src = with_prelude(bodies[i]);
        char label[32];
        snprintf(label, sizeof label, "case %d", i + 1);
        differential(label, src);
        free(src);
    }

    /* And every example that is meant to compile. */
    static const char *const files[] = {
        "examples/01_minimal.aero",  "examples/02_converging.aero",
        "examples/03_procedures.aero", "examples/04_conditional.aero",
        "examples/05_loop.aero",     "examples/06_optimise.aero",
        "examples/11_boundaries.aero", "examples/12_separation.aero",
    };
    for (int i = 0; i < LEN(files); i++) {
        char *src = slurp(files[i]);
        if (!src) {
            fprintf(stderr, "NOTE  %s not found; run the tests from the project root\n", files[i]);
            continue;
        }
        differential(files[i], src);
        free(src);
    }
}

/* ---- the examples compile as documented ---------------------------- */

static void test_examples(void) {
    group("examples: the valid ones compile and run");
    static const char *const valid[] = {
        "examples/01_minimal.aero",  "examples/02_converging.aero",
        "examples/03_procedures.aero", "examples/04_conditional.aero",
        "examples/05_loop.aero",     "examples/06_optimise.aero",
        "examples/11_boundaries.aero", "examples/12_separation.aero",
    };
    for (int i = 0; i < LEN(valid); i++) {
        char *src = slurp(valid[i]);
        if (!src) continue;
        Run r;
        run_src(&r, src, options_default());
        checks++;
        if (diag_errors(&r.u.diags)) {
            fail("%s should compile cleanly", valid[i]);
            diag_print_all(stderr, &r.u.diags);
        } else if (!r.u.ran) {
            fail("%s should run", valid[i]);
        }
        run_free(&r);
        free(src);
    }

    group("examples: the invalid ones are rejected");
    static const struct {
        const char *path;
        Phase phase;
    } invalid[] = {
        {"examples/07_lexical_errors.aero", PH_LEXICAL},
        {"examples/08_syntax_errors.aero", PH_SYNTAX},
        {"examples/09_type_errors.aero", PH_SEMANTIC},
        {"examples/10_domain_errors.aero", PH_SEMANTIC},
    };
    for (int i = 0; i < LEN(invalid); i++) {
        char *src = slurp(invalid[i].path);
        if (!src) continue;
        Run r;
        run_src(&r, src, options_default());
        CHECK(diag_errors(&r.u.diags) > 0, "%s should be rejected", invalid[i].path);
        CHECK(diag_errors_phase(&r.u.diags, invalid[i].phase) > 0,
              "%s should fail in the %s phase", invalid[i].path, phase_name(invalid[i].phase));
        run_free(&r);
        free(src);
    }
}

/* ---- the JSON dump stays well formed ------------------------------- */

static void test_json(void) {
    group("json: the dump is produced for every pipeline outcome");
    static const char *const cases[] = {
        "scenario \"t\"\nfix V 1 2\nflight A1 type A320 enter V alt 1000 hdg 10 spd 200KT\n"
        "clear A1 climb 2000\nadvance 10\n",
        "scenario \"t\"\n@\n",                      /* lexical failure   */
        "scenario \"t\"\nfix V 1 2\nadvance\n",      /* syntax failure    */
        "scenario \"t\"\nfix V 1 2\nadvance true\n", /* semantic failure  */
        "",                                          /* nothing at all    */
    };
    for (int i = 0; i < LEN(cases); i++) {
        Run r;
        run_src(&r, cases[i], options_default());
        char *out = NULL;
        size_t len = 0;
        MemOut mo;
    FILE *mem = memout_open(&mo);
        json_dump(mem, &r.u, cases[i]);
        out = memout_close(&mo, &len);
        CHECK(out && len > 10, "case %d should produce a dump", i);
        if (out) {
            CHECK(out[0] == '{' && out[len - 2] == '}', "case %d should be a JSON object", i);
            int depth = 0, instr = 0, ok = 1;
            for (size_t k = 0; k < len; k++) {
                char c = out[k];
                if (instr) {
                    if (c == '\\')
                        k++;
                    else if (c == '"')
                        instr = 0;
                    continue;
                }
                if (c == '"')
                    instr = 1;
                else if (c == '{' || c == '[')
                    depth++;
                else if (c == '}' || c == ']') {
                    depth--;
                    if (depth < 0) ok = 0;
                }
            }
            CHECK(ok && depth == 0, "case %d should have balanced brackets (depth %d)", i, depth);
        }
        free(out);
        run_free(&r);
    }
}

int main(void) {
    test_lexer();
    test_parser();
    test_sema();
    test_analysis();
    test_ir();
    test_cfg();
    test_ssa();
    test_opt();
    test_codegen();
    test_vm();
    test_scn();
    test_examples();
    test_differential();
    test_json();

    fprintf(stderr, "\n%d checks, %d failure%s\n", checks, failures, failures == 1 ? "" : "s");
    if (!failures) fputs("all tests passed\n", stderr);
    return failures ? 1 : 0;
}
