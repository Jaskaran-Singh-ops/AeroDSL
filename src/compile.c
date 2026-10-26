/*
 * The driver: runs the pipeline and stops at the first phase that fails.
 *
 * The unoptimised three-address code is kept alongside the optimised code so
 * that the demonstration UI can show both. That is the only reason for the
 * clone; nothing in the compiler needs it.
 */
#include "aerodsl.h"

Options options_default(void) { return (Options){1, 1, 0}; }

/* Copies the generated code so the optimiser can work on one and leave the
 * other untouched. Only the instruction list and the register names are
 * copied; analyses and bytecode are rebuilt by the phases that need them. */
static void funcs_clone(const FuncList *src, FuncList *dst) {
    memset(dst, 0, sizeof *dst);
    for (int i = 0; i < src->count; i++) {
        Func f = src->items[i];
        f.blocks = NULL;
        f.nblocks = 0;
        f.bc = NULL;
        f.nbc = f.bc_cap = 0;
        f.nslots = 0;

        f.ir.items = xmalloc((size_t)(src->items[i].ir.count > 0 ? src->items[i].ir.count : 1) *
                             sizeof *f.ir.items);
        memcpy(f.ir.items, src->items[i].ir.items,
               (size_t)src->items[i].ir.count * sizeof *f.ir.items);
        f.ir.count = f.ir.cap = src->items[i].ir.count;

        if (src->items[i].nvnames > 0) {
            f.vnames = xmalloc((size_t)src->items[i].nvnames * sizeof *f.vnames);
            memcpy(f.vnames, src->items[i].vnames,
                   (size_t)src->items[i].nvnames * sizeof *f.vnames);
        }
        PUSH(*dst, f);
    }
}

static void analyse_all(FuncList *fns) {
    for (int i = 0; i < fns->count; i++) {
        cfg_build(&fns->items[i]);
        cfg_liveness(&fns->items[i]);
        cfg_dominators(&fns->items[i]);
        cfg_loops(&fns->items[i]);
    }
}

void unit_compile(Unit *u, const char *src, Options opt) {
    memset(u, 0, sizeof *u);
    symtab_init(&u->st);

    tokenize(src, &u->tokens, &u->diags);
    if (diag_errors(&u->diags)) return;
    u->lexed = 1;

    parse(&u->tokens, &u->prog, &u->st, &u->diags);
    /* Name-resolution errors are reported by the parser but do not stop
     * type checking: the affected expressions simply have no symbol, and
     * sema is written to tolerate that. A *syntax* error does stop it,
     * because the tree past that point is not trustworthy. */
    if (diag_errors_phase(&u->diags, PH_SYNTAX)) return;
    u->parsed = 1;

    sema_run(&u->prog, &u->st, &u->diags);
    if (diag_errors(&u->diags)) return;
    u->checked = 1;

    ir_generate(&u->prog, &u->funcs);
    analyse_all(&u->funcs);

    funcs_clone(&u->funcs, &u->opt_funcs);
    if (opt.optimise)
        opt_run_all(&u->opt_funcs, &u->stats, &u->diags);
    else
        analyse_all(&u->opt_funcs);

    analyse_ir(&u->opt_funcs, &u->prog, &u->diags);
    if (diag_errors(&u->diags)) return;
    u->lowered = 1;

    codegen_run_all(&u->opt_funcs, &u->stats);

    if (opt.simulate) {
        vm_run(&u->prog, &u->opt_funcs, &u->vm, &u->diags);
        u->ran = 1;
    }
}

void unit_free(Unit *u) {
    vm_result_free(&u->vm);
    funcs_free(&u->opt_funcs);
    funcs_free(&u->funcs);
    program_free(&u->prog);
    symtab_free(&u->st);
    tokens_free(&u->tokens);
    diag_free(&u->diags);
}
