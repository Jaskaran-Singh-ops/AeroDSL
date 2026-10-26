/*
 * Command-line interface.
 *
 *   aerodsl scenario.aero              compile, simulate, write the scenario file
 *   aerodsl --phases scenario.aero     show every phase of the pipeline
 *   aerodsl --ir scenario.aero         just the three-address code
 *   aerodsl --serve 8080               the demonstration UI
 *
 * With no phase selected the scenario file goes to standard output and
 * diagnostics to standard error, so `aerodsl in.aero > out.scn` works.
 */
#include "aerodsl.h"

typedef struct {
    int tokens, ast, symbols, ir, opt_ir, cfg, ssa, nextuse, bc, trace, scn, stats, json;
} Show;

static void usage(FILE *f) {
    fputs(
        "AeroDSL -- a compiler for air-traffic-control training scenarios.\n"
        "Academic prototype. Not for operational use.\n"
        "\n"
        "usage: aerodsl [options] [file]\n"
        "       aerodsl --serve [port]\n"
        "\n"
        "Reads standard input when no file is given.\n"
        "\n"
        "phase output (any combination; default is --scn):\n"
        "  --tokens     the token stream from the lexer\n"
        "  --ast        the abstract syntax tree, with types\n"
        "  --symbols    the symbol table\n"
        "  --ir         three-address code, before optimisation\n"
        "  --opt-ir     three-address code, after optimisation\n"
        "  --cfg        basic blocks, dominators and liveness\n"
        "  --ssa        static single assignment form, with phi functions\n"
        "  --nextuse    next-use information used by the register allocator\n"
        "  --bc         AeroVM bytecode\n"
        "  --trace      the simulation trace and final state\n"
        "  --scn        the generated scenario file\n"
        "  --stats      what each optimisation pass changed\n"
        "  --phases     all of the above, in pipeline order\n"
        "  --json       every phase as one JSON object\n"
        "\n"
        "pipeline control:\n"
        "  --no-opt     skip the optimiser\n"
        "  --no-sim     do not run the simulation\n"
        "\n"
        "exit status: 0 success, 1 diagnostics reported an error, 2 could not read input\n",
        f);
}

static void print_stats(FILE *f, const OptStats *s) {
    fputs("optimisation summary\n", f);
    struct {
        const char *what;
        int n;
    } rows[] = {{"constants folded", s->folded},
                {"operands replaced by propagated constants", s->propagated},
                {"common subexpressions eliminated", s->cse},
                {"copies propagated", s->copies},
                {"algebraic simplifications", s->algebraic},
                {"dead instructions removed", s->dead},
                {"instructions hoisted out of loops", s->hoisted},
                {"jumps threaded or removed", s->jumps},
                {"unreachable blocks removed", s->blocks_removed},
                {"peephole rewrites", s->peephole}};
    for (int i = 0; i < LEN(rows); i++) fprintf(f, "  %-44s %d\n", rows[i].what, rows[i].n);
}

static void print_tokens(FILE *f, const TokenList *t) {
    fprintf(f, "token stream: %d token%s\n", t->count, t->count == 1 ? "" : "s");
    for (int i = 0; i < t->count; i++) {
        const Token *tk = &t->items[i];
        fprintf(f, "  %3d:%-3d %-12s %.*s", tk->line, tk->col, token_name(tk->type), tk->text.len,
                tk->text.ptr);
        if (tk->type == T_NUMBER || tk->type == T_FLEVEL || tk->type == T_SPEED)
            fprintf(f, "   value %g", tk->num);
        fputc('\n', f);
    }
}

static void section(FILE *f, const char *title) {
    fprintf(f, "\n===== %s =====\n", title);
}

int main(int argc, char **argv) {
    Show show = {0};
    Options opt = options_default();
    const char *path = NULL;
    int any_show = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--serve") == 0) {
            int port = (i + 1 < argc) ? atoi(argv[i + 1]) : 8080;
            return serve(port > 0 ? port : 8080);
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(a, "--tokens") == 0) {
            show.tokens = any_show = 1;
        } else if (strcmp(a, "--ast") == 0) {
            show.ast = any_show = 1;
        } else if (strcmp(a, "--symbols") == 0) {
            show.symbols = any_show = 1;
        } else if (strcmp(a, "--ir") == 0) {
            show.ir = any_show = 1;
        } else if (strcmp(a, "--opt-ir") == 0) {
            show.opt_ir = any_show = 1;
        } else if (strcmp(a, "--cfg") == 0) {
            show.cfg = any_show = 1;
        } else if (strcmp(a, "--ssa") == 0) {
            show.ssa = any_show = 1;
        } else if (strcmp(a, "--nextuse") == 0) {
            show.nextuse = any_show = 1;
        } else if (strcmp(a, "--bc") == 0) {
            show.bc = any_show = 1;
        } else if (strcmp(a, "--trace") == 0) {
            show.trace = any_show = 1;
        } else if (strcmp(a, "--scn") == 0) {
            show.scn = any_show = 1;
        } else if (strcmp(a, "--stats") == 0) {
            show.stats = any_show = 1;
        } else if (strcmp(a, "--json") == 0) {
            show.json = any_show = 1;
        } else if (strcmp(a, "--phases") == 0) {
            show.tokens = show.ast = show.symbols = show.ir = show.opt_ir = 1;
            show.cfg = show.ssa = show.nextuse = show.bc = show.trace = show.scn = 1;
            show.stats = 1;
            any_show = 1;
        } else if (strcmp(a, "--no-opt") == 0) {
            opt.optimise = 0;
        } else if (strcmp(a, "--no-sim") == 0) {
            opt.simulate = 0;
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "aerodsl: unknown option '%s'\n", a);
            usage(stderr);
            return 2;
        } else {
            path = a;
        }
    }
    if (!any_show) show.scn = 1;

    FILE *in = path ? fopen(path, "r") : stdin;
    if (!in) {
        fprintf(stderr, "aerodsl: cannot read %s: ", path);
        perror(NULL);
        return 2;
    }
    char *src = read_all(in);
    if (in != stdin) fclose(in);

    Unit u;
    unit_compile(&u, src, opt);

    if (show.json) {
        diag_sort(&u.diags);
        json_dump(stdout, &u, src);
        int status = diag_errors(&u.diags) ? 1 : 0;
        unit_free(&u);
        free(src);
        return status;
    }

    /* Diagnostics go to stderr so that a redirected scenario file is clean,
     * in source order rather than in the order the phases found them. */
    diag_sort(&u.diags);
    diag_print_all(stderr, &u.diags);

    int multi = 0;
    {
        int n = show.tokens + show.ast + show.symbols + show.ir + show.opt_ir + show.cfg +
                show.ssa + show.nextuse + show.bc + show.trace + show.scn + show.stats;
        multi = n > 1;
    }
#define SECTION(name)                 \
    do {                              \
        if (multi) section(stdout, name); \
    } while (0)

    if (show.tokens && u.lexed) {
        SECTION("1. lexical analysis: tokens");
        print_tokens(stdout, &u.tokens);
    }
    if (show.ast && u.parsed) {
        SECTION("2. syntax analysis: abstract syntax tree");
        ast_print(stdout, &u.prog);
    }
    if (show.symbols && u.parsed) {
        SECTION("3. semantic analysis: symbol table");
        symtab_print(stdout, &u.st);
    }
    if (show.ir && u.checked) {
        SECTION("4. intermediate code: three-address code");
        ir_print_all(stdout, &u.prog, &u.funcs);
    }
    if (show.cfg && u.checked) {
        SECTION("5. control-flow graph of the optimised code");
        for (int i = 0; i < u.opt_funcs.count; i++) {
            if (i) fputc('\n', stdout);
            cfg_print(stdout, &u.opt_funcs.items[i]);
        }
    }
    if (show.opt_ir && u.checked) {
        SECTION("6. optimised three-address code");
        ir_print_all(stdout, &u.prog, &u.opt_funcs);
    }
    if (show.ssa && u.checked) {
        SECTION("7. static single assignment form");
        for (int i = 0; i < u.opt_funcs.count; i++) {
            if (i) fputc('\n', stdout);
            ssa_print(stdout, &u.prog, &u.opt_funcs.items[i]);
        }
    }
    if (show.stats && u.checked) {
        SECTION("8. optimisation summary");
        print_stats(stdout, &u.stats);
    }
    if (show.nextuse && u.lowered) {
        SECTION("9. next-use information");
        for (int i = 0; i < u.opt_funcs.count; i++) {
            if (i) fputc('\n', stdout);
            nextuse_print(stdout, &u.opt_funcs.items[i]);
        }
    }
    if (show.bc && u.lowered) {
        SECTION("10. code generation: AeroVM bytecode");
        bc_print_all(stdout, &u.prog, &u.opt_funcs);
    }
    if (show.trace && u.ran) {
        SECTION("11. execution: simulation trace");
        vm_print_trace(stdout, &u.prog, &u.vm);
    }
    if (show.scn && u.ran) {
        SECTION("12. output: generated scenario file");
        scn_emit(stdout, &u.prog, &u.vm);
    }
#undef SECTION

    int status = diag_errors(&u.diags) ? 1 : 0;
    unit_free(&u);
    free(src);
    return status;
}
