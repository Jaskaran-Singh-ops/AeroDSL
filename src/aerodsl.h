/*
 * AeroDSL -- a language for air-traffic-control training scenarios.
 *
 * This header is the project's design in one file: it declares the data
 * structures that flow between compiler phases and the entry point of each
 * phase. Read it alongside docs/SPEC.md (the language) and
 * docs/ARCHITECTURE.md (the pipeline).
 *
 * Pipeline:  source -> lexer -> parser -> AST -> sema -> TAC -> CFG
 *                   -> optimiser -> regalloc -> bytecode -> AeroVM
 *                   -> scenario file
 *
 * Academic prototype. Not for real aircraft, live ATC systems, or any
 * safety-critical use.
 */
#ifndef AERODSL_H
#define AERODSL_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Small utilities
 * ------------------------------------------------------------------ */

/* Append to a zero-initialised {items, count, cap} list. Aborts on OOM. */
#define PUSH(list, ...)                                                            \
    do {                                                                           \
        if ((list).count == (list).cap) {                                          \
            (list).cap = (list).cap ? 2 * (list).cap : 8;                          \
            (list).items =                                                         \
                realloc((list).items, (size_t)(list).cap * sizeof *(list).items);  \
            if (!(list).items) abort();                                            \
        }                                                                          \
        (list).items[(list).count++] = (__VA_ARGS__);                              \
    } while (0)

/* Append to an explicit {array, count, cap} triple. */
#define PUSHN(arr, n, cap, ...)                                              \
    do {                                                                     \
        if ((n) == (cap)) {                                                  \
            (cap) = (cap) ? 2 * (cap) : 8;                                   \
            (arr) = xrealloc((arr), (size_t)(cap) * sizeof *(arr));          \
        }                                                                    \
        (arr)[(n)++] = (__VA_ARGS__);                                        \
    } while (0)

#define LEN(a) ((int)(sizeof(a) / sizeof *(a)))

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);

/* A view into the source text. The source buffer must outlive every Str. */
typedef struct {
    const char *ptr;
    int len;
} Str;

int str_is(Str s, const char *lit);
int str_eq(Str a, Str b);
/* Writes at most n-1 bytes of s plus a NUL into buf and returns buf. */
char *str_cstr(Str s, char *buf, size_t n);

/* A growable list of ints, used for backpatch lists and bit-set-free sets. */
typedef struct {
    int *items;
    int count, cap;
} IntList;

void intlist_push(IntList *l, int v);
int intlist_has(const IntList *l, int v);
void intlist_free(IntList *l);

/* Fixed-size bit sets for the dataflow analyses. */
typedef struct {
    unsigned *w;
    int nbits, nwords;
} BitSet;

void bs_init(BitSet *b, int nbits);
void bs_free(BitSet *b);
void bs_clear(BitSet *b);
void bs_copy(BitSet *dst, const BitSet *src);
int bs_get(const BitSet *b, int i);
void bs_set(BitSet *b, int i);
void bs_unset(BitSet *b, int i);
int bs_union(BitSet *dst, const BitSet *src);     /* returns 1 if dst changed */
int bs_intersect(BitSet *dst, const BitSet *src); /* returns 1 if dst changed */
void bs_fill(BitSet *b);
int bs_count(const BitSet *b);
int bs_eq(const BitSet *a, const BitSet *b);

/* ------------------------------------------------------------------ *
 * Diagnostics  (src/diag.c)
 * ------------------------------------------------------------------ */

typedef enum {
    PH_LEXICAL,
    PH_SYNTAX,
    PH_SEMANTIC,
    PH_RUNTIME,
    PH_VERIFY /* assertion / separation findings from the simulation */
} Phase;

typedef enum { SEV_ERROR, SEV_WARNING, SEV_NOTE } Severity;

typedef struct {
    Phase phase;
    Severity sev;
    int line, col;
    char message[256];
} Diagnostic;

typedef struct {
    Diagnostic *items;
    int count, cap;
} DiagList;

void diag_add(DiagList *d, Phase phase, Severity sev, int line, int col, const char *fmt, ...)
    __attribute__((format(printf, 6, 7)));
void diag_print(FILE *f, const Diagnostic *d);
void diag_print_all(FILE *f, const DiagList *d);
int diag_errors(const DiagList *d);
int diag_errors_phase(const DiagList *d, Phase p);
/* True if something has already been reported on this source line, used to
 * keep two phases from reporting the same mistake twice. */
int diag_has_at(const DiagList *d, int line);
/* Orders diagnostics by source position for display. Stable, so two
 * messages on the same spot keep the order the phases produced them, and
 * messages with no position (whole-program notes) sort last. */
void diag_sort(DiagList *d);
const char *phase_name(Phase p);
const char *sev_name(Severity s);
void diag_free(DiagList *d);

/* ------------------------------------------------------------------ *
 * Lexer  (src/lexer.c, SPEC section 3)
 * ------------------------------------------------------------------ */

typedef enum {
    /* literals and names */
    T_NUMBER,   /* 270, 12.95          */
    T_FLEVEL,   /* FL320  -> 32000 ft  */
    T_SPEED,    /* 280KT  -> 280 kt    */
    T_IDENT,    /* lower_snake         */
    T_CALLSIGN, /* AI202, VOMAN        */
    T_STRING,   /* "text"              */
    /* keywords */
    T_SCENARIO, T_FIX, T_FLIGHT, T_TYPE, T_ENTER, T_ALT, T_HDG, T_SPD, T_LAT, T_LON,
    T_PROC, T_LET, T_CLEAR, T_CLIMB, T_DESCEND, T_HEADING, T_SPEED_KW, T_DIRECT,
    T_ADVANCE, T_IF, T_THEN, T_ELSE, T_WHILE, T_DO, T_END,
    T_ASSERT, T_CALL, T_PRINT, T_RETURN,
    T_AND, T_OR, T_NOT, T_TRUE, T_FALSE,
    /* `num` and `bool` are only type names; `flight` and `fix` double as the
     * declaration keywords T_FLIGHT and T_FIX, which the parser disambiguates
     * by position (SPEC section 4.4). */
    T_TY_NUM, T_TY_BOOL,
    /* operators and punctuation */
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_LT, T_LE, T_GT, T_GE, T_EQEQ, T_NE, T_ASSIGN,
    T_LPAREN, T_RPAREN, T_COMMA, T_DOT,
    T_EOF
} TokenType;

typedef struct {
    TokenType type;
    Str text;    /* the lexeme as written */
    double num;  /* T_NUMBER / T_FLEVEL / T_SPEED: the numeric attribute */
    int line, col;
} Token;

typedef struct {
    Token *items;
    int count, cap;
} TokenList;

const char *token_name(TokenType t);
/* Appends tokens (always ending in T_EOF) to out, lexical errors to diags. */
void tokenize(const char *src, TokenList *out, DiagList *diags);
void tokens_free(TokenList *t);

/* ------------------------------------------------------------------ *
 * Types and symbols  (src/sema.c, SPEC sections 5-6)
 * ------------------------------------------------------------------ */

typedef enum { TY_ERROR, TY_NUM, TY_BOOL, TY_FLIGHT, TY_FIX, TY_VOID } Type;

const char *type_name(Type t);

typedef enum { SYM_VAR, SYM_PARAM, SYM_FLIGHT, SYM_FIX, SYM_PROC } SymKind;

typedef struct Sym {
    SymKind kind;
    Type type;
    Str name;
    int id;    /* VAR/PARAM: vreg index.  FLIGHT/FIX: table index.  PROC: proc index. */
    int scope; /* scope depth where it was declared */
    int line, col;
    int used;
    int assigned;
    struct ProcDecl *proc; /* SYM_PROC only */
} Sym;

typedef struct {
    Sym **items;
    int count, cap;
} SymList;

/* A scoped symbol table. Scopes are a stack; lookup walks inner to outer. */
typedef struct {
    SymList all;   /* every symbol ever declared; owns them, for dumps */
    SymList stack; /* the symbols currently in scope, innermost last */
    int *starts;   /* starts[d] = index in stack where scope depth d begins */
    int depth, cap;
    /* Symbols below this stack index are only visible if they are global
     * kinds (aircraft, waypoints, procedures). Raised while parsing a
     * procedure body so that a procedure cannot see the main body's
     * variables, which live in a different activation record (SPEC 6.1). */
    int barrier;
} SymTab;

void symtab_init(SymTab *st);
void symtab_free(SymTab *st);
void symtab_push(SymTab *st);
void symtab_pop(SymTab *st);
Sym *symtab_declare(SymTab *st, SymKind kind, Type type, Str name, int line, int col);
Sym *symtab_lookup(const SymTab *st, Str name);
Sym *symtab_lookup_local(const SymTab *st, Str name);
void symtab_print(FILE *f, const SymTab *st);
const char *sym_kind_name(SymKind k);

/* ------------------------------------------------------------------ *
 * Abstract syntax tree  (src/parser.c builds it, src/ast.c prints it)
 * ------------------------------------------------------------------ */

/* Binary and unary operators, shared by the AST and the IR. */
typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_LT, OP_LE, OP_GT, OP_GE, OP_EQ, OP_NE,
    OP_AND, OP_OR,  /* AST only: short-circuited away during IR generation */
    OP_NEG, OP_NOT
} BinOp;

const char *op_symbol(BinOp op);

/* Readable flight state fields: flight.alt and friends. */
typedef enum { FLD_ALT, FLD_HDG, FLD_SPD, FLD_LAT, FLD_LON, FLD_COUNT } Field;

const char *field_name(Field f);

/* Built-in functions. */
typedef enum { BI_VSEP, BI_HSEP, BI_DIST, BI_BEARING, BI_ABS, BI_MIN, BI_MAX, BI_NOW, BI_COUNT } Builtin;

typedef struct {
    const char *name;
    int arity;
    Type ret;
    Type args[2];
    const char *unit;
    const char *doc;
} BuiltinInfo;

extern const BuiltinInfo BUILTINS[BI_COUNT];

/* The five clearance kinds. */
typedef enum { CL_CLIMB, CL_DESCEND, CL_HEADING, CL_SPEED, CL_DIRECT, CL_COUNT } ClearKind;

typedef struct {
    const char *name;
    Type operand;
    const char *unit;
    double lo, hi; /* valid operand range, unused for CL_DIRECT */
} ClearInfo;

extern const ClearInfo CLEARS[CL_COUNT];

typedef enum {
    EX_NUM,
    EX_BOOL,
    EX_SYM,     /* variable, parameter, flight or fix reference */
    EX_BIN,
    EX_UN,
    EX_FIELD,   /* flight.alt */
    EX_BUILTIN
} ExprKind;

typedef struct Expr {
    ExprKind kind;
    Type type;
    int line, col;
    union {
        double num;
        int bval;
        Sym *sym;
        struct {
            BinOp op;
            struct Expr *l, *r;
        } bin;
        struct {
            BinOp op;
            struct Expr *e;
        } un;
        struct {
            struct Expr *obj;
            Field field;
        } fld;
        struct {
            Builtin fn;
            struct Expr *args[2];
            int nargs;
        } call;
    } as;
} Expr;

typedef enum {
    ST_LET,
    ST_ASSIGN,
    ST_ENTER, /* a `flight` declaration, which also executes in place */
    ST_CLEAR,
    ST_ADVANCE,
    ST_IF,
    ST_WHILE,
    ST_ASSERT,
    ST_CALL,
    ST_PRINT,
    ST_RETURN
} StmtKind;

typedef struct Stmt Stmt;

typedef struct {
    Stmt **items;
    int count, cap;
} StmtList;

struct Stmt {
    StmtKind kind;
    int line, col;
    union {
        struct {
            Sym *sym;
            Expr *init;
        } let; /* also ST_ASSIGN */
        struct {
            int flight; /* index into Program.flights */
        } enter;
        struct {
            Expr *flight;
            ClearKind kind;
            Expr *operand;
        } clear;
        struct {
            Expr *secs;
        } advance;
        struct {
            Expr *cond;
            StmtList then_body, else_body;
            int has_else;
        } ifs;
        struct {
            Expr *cond;
            StmtList body;
        } whiles;
        struct {
            Expr *cond;
            int msg; /* index into Program.strings, or -1 */
        } assertion;
        struct {
            Sym *proc;
            Expr *args[8];
            int nargs;
        } call;
        struct {
            Expr *args[8];
            int msgs[8]; /* string index, or -1 if args[i] is an expression */
            int nargs;
        } print;
    } as;
};

typedef struct ProcDecl {
    Str name;
    Sym *sym;
    Sym *params[8];
    int nparams;
    StmtList body;
    int line, col;
    int nvregs; /* assigned during semantic analysis */
} ProcDecl;

typedef struct {
    Str name;
    double lat, lon;
    int line, col;
} FixDecl;

typedef struct {
    Str name;
    Str type;    /* aircraft type, empty if the declaration omitted it */
    int fix;     /* index into Program.fixes, -1 if unresolved */
    Expr *alt, *hdg, *spd;
    int line, col;
} FlightDecl;

typedef struct {
    Str scenario; /* the scenario title, without quotes */

    FixDecl *fixes;
    int nfixes, fixes_cap;

    FlightDecl *flights;
    int nflights, flights_cap;

    ProcDecl **procs;
    int nprocs, procs_cap;

    StmtList main_body;

    char **strings; /* string literals, NUL-terminated and owned */
    int nstrings, strings_cap;

    int nvregs; /* virtual registers needed by main */
} Program;

Expr *expr_new(ExprKind kind, int line, int col);
Stmt *stmt_new(StmtKind kind, int line, int col);
void stmtlist_push(StmtList *l, Stmt *s);
/* Frees an expression tree. Needed by the parser's error paths, which
 * discard a subexpression they have already built. */
void expr_free(Expr *e);
int program_add_string(Program *p, Str s);
void program_free(Program *p);

/* Pretty-prints the AST as an indented tree (src/ast.c). */
void ast_print(FILE *f, const Program *p);

/* ------------------------------------------------------------------ *
 * Parser  (src/parser.c, SPEC section 4)
 * ------------------------------------------------------------------ */

/* Parses tokens into out. The symbol table is filled as declarations are
 * seen, so name resolution happens during the parse; type checking and the
 * domain rules are left to sema. out is partial when errors were reported. */
void parse(const TokenList *tokens, Program *out, SymTab *st, DiagList *diags);

/* ------------------------------------------------------------------ *
 * Semantic analysis  (src/sema.c, SPEC sections 5-7)
 * ------------------------------------------------------------------ */

void sema_run(Program *p, SymTab *st, DiagList *diags);

/* ------------------------------------------------------------------ *
 * Three-address code  (src/ir.c, SPEC section 8)
 * ------------------------------------------------------------------ */

typedef enum {
    IR_CONST,    /* dst = k            */
    IR_COPY,     /* dst = a            */
    IR_BIN,      /* dst = a <op> b     */
    IR_UN,       /* dst = <op> a       */
    IR_FIELD,    /* dst = a.field      */
    IR_BUILTIN,  /* dst = fn(a, b)     */
    IR_LABEL,    /* L<n>:              */
    IR_GOTO,     /* goto L             */
    IR_IFTRUE,   /* if a goto L        */
    IR_IFFALSE,  /* ifFalse a goto L   */
    IR_IFREL,    /* if a <rel> b goto L   (created by the peephole fuser)   */
    IR_ENTER,    /* enter a   (preceded by three `param`: alt, hdg, spd) */
    IR_CLEAR,    /* clear a, <kind>, b;  bop = 1 once analysis.c has checked
                  * that a climb climbs, so the VM does not repeat it */
    IR_ADVANCE,  /* advance a          */
    IR_ASSERT,   /* assert a, "msg"    */
    IR_PRINT,    /* print a            */
    IR_PRINTSTR, /* print "msg"        */
    IR_PARAM,    /* param a            */
    IR_CALL,     /* call p, n          */
    IR_RETURN,   /* return             */
    IR_NOP
} IrOp;

const char *ir_op_name(IrOp op);

typedef enum {
    AR_NONE,
    AR_VREG,   /* virtual register id */
    AR_CONST,  /* immediate k */
    AR_LABEL,  /* label id */
    AR_PROC,   /* procedure index */
    AR_STR,    /* string-table index */
    AR_FLIGHT, /* flight-table index */
    AR_FIX,    /* fix-table index */
    AR_FIELD,  /* Field */
    AR_CLEAR,  /* ClearKind */
    AR_BI,     /* Builtin */
    AR_INT     /* a plain integer (argument counts) */
} ArgKind;

typedef struct {
    ArgKind kind;
    int id;
    double k;
} Arg;

typedef struct {
    IrOp op;
    BinOp bop; /* IR_BIN, IR_UN, IR_IFREL */
    int aux;   /* IR_FIELD: Field.  IR_BUILTIN: Builtin.  IR_CLEAR: ClearKind.
                * IR_CALL: argument count.  Unused elsewhere. */
    Arg dst, a, b;
    int line, col;
} Ir;

typedef struct {
    Ir *items;
    int count, cap;
} IrList;

/* A basic block (src/cfg.c). */
typedef struct {
    int first, last; /* inclusive range of IR indices */
    IntList succ, pred;
    /* dataflow */
    BitSet use, def, live_in, live_out;
    /* dominators */
    BitSet dom;
    int idom;
    int loop_header; /* 1 if some back edge targets this block */
    int depth;       /* loop nesting depth */
    int reachable;
} Block;

/* One compiled procedure. Procedure 0 is the implicit "main". */
typedef struct {
    Str name;
    int nparams;
    int nvregs;
    /* Source name of each virtual register that came from a declaration, so
     * that a dump reads `cruise = 32000` rather than `t0 = 32000`. Compiler
     * temporaries have no entry and print as tN. */
    Str *vnames;
    int nvnames;
    IrList ir;

    Block *blocks;
    int nblocks;

    /* filled by codegen. Every virtual register has a home frame slot, and
     * the slot of vreg v is v, which puts a procedure's parameters in slots
     * 0..nparams-1 where the caller writes them. */
    int nslots;
    struct Bc *bc;
    int nbc, bc_cap;
} Func;

typedef struct {
    Func *items;
    int count, cap;
} FuncList;

/* Translates the AST into three-address code, one Func per procedure plus
 * main. Uses backpatching for boolean expressions and control flow. */
void ir_generate(const Program *p, FuncList *out);
void ir_print(FILE *f, const Program *p, const Func *fn);
/* Prints a virtual register by its source name when it has one, else tN. */
void ir_print_vreg(FILE *f, const Func *fn, int id);
/* Prints one instruction, without a trailing newline. */
void ir_print_one(FILE *f, const Program *p, const Func *fn, const Ir *ir);
void ir_print_all(FILE *f, const Program *p, const FuncList *fns);
void funcs_free(FuncList *fns);
int ir_arg_eq(Arg a, Arg b);

/* ------------------------------------------------------------------ *
 * Control-flow graph and dataflow  (src/cfg.c, SPEC section 9)
 * ------------------------------------------------------------------ */

void cfg_build(Func *fn);
void cfg_free(Func *fn);
void cfg_liveness(Func *fn);
void cfg_dominators(Func *fn);
void cfg_loops(Func *fn);
void cfg_print(FILE *f, const Func *fn);
int cfg_block_of(const Func *fn, int ir_index);
/* Collects back edges (an edge tail -> header where header dominates tail);
 * each one identifies a natural loop. Returns how many were found. */
int cfg_back_edges(const Func *fn, int *tails, int *headers, int max);
/* Fills `body` with the natural loop of the back edge tail -> header: the
 * blocks that reach tail without passing through header, plus header. */
void cfg_natural_loop(const Func *fn, int header, int tail, BitSet *body);
/* What an instruction reads and writes, used by liveness, the optimiser and
 * the register allocator. `reads` must have room for two. */
int ir_reads(const Ir *ir, int reads[2]);
int ir_writes(const Ir *ir);
int ir_has_side_effect(const Ir *ir);
int ir_is_barrier(const Ir *ir);
int ir_is_state_dependent(const Ir *ir);
int ir_is_jump(const Ir *ir);

/* ------------------------------------------------------------------ *
 * Static single assignment  (src/ssa.c, SPEC section 9.1)
 * ------------------------------------------------------------------ *
 * Builds and prints the SSA form of a function: dominance frontiers,
 * phi-function placement and version renaming. This is a view of the code,
 * not a replacement for it -- the optimiser works on the ordinary
 * three-address code, and SPEC section 14 says so plainly.
 */
/* Takes a mutable Func because it recomputes the dominator information it
 * needs rather than trusting whatever the last phase left behind. */
void ssa_print(FILE *f, const Program *p, Func *fn);
/* Dominance frontiers, which is where phi functions go. The caller owns the
 * returned array of nblocks bit sets and frees it with bs_free on each. */
BitSet *cfg_dominance_frontiers(const Func *fn);

/* ------------------------------------------------------------------ *
 * Domain-specific static analysis  (src/analysis.c, SPEC section 10.8)
 * ------------------------------------------------------------------ *
 * Runs on the optimised three-address code, where control flow is explicit
 * and constant propagation has already replaced variables by their values.
 * That is what lets it range-check an operand written as a variable, and
 * decide whether two clearances reach each other with no `advance` between.
 */
/* Takes a mutable FuncList: it marks each clearance whose direction it was
 * able to check, so the VM knows which ones are left for it. */
void analyse_ir(FuncList *fns, const Program *p, DiagList *diags);

/* ------------------------------------------------------------------ *
 * Optimiser  (src/opt.c, SPEC section 10)
 * ------------------------------------------------------------------ */

typedef struct {
    int folded;         /* constant folding            */
    int propagated;     /* constant propagation        */
    int cse;            /* common subexpressions       */
    int copies;         /* copy propagation            */
    int algebraic;      /* algebraic simplification    */
    int dead;           /* dead code eliminated        */
    int hoisted;        /* loop-invariant instructions */
    int jumps;          /* jumps threaded              */
    int blocks_removed; /* unreachable blocks          */
    int peephole;       /* bytecode peephole           */
} OptStats;

/* Runs every pass to a fixed point. Set stats to NULL if not wanted. */
void opt_run(Func *fn, OptStats *stats, DiagList *diags);
void opt_run_all(FuncList *fns, OptStats *stats, DiagList *diags);

/* ------------------------------------------------------------------ *
 * Code generation  (src/codegen.c, SPEC section 11)
 * ------------------------------------------------------------------ */

#define NREGS 16

typedef enum {
    BC_LOADK,  /* r[a] = k            */
    BC_MOVE,   /* r[a] = r[b]         */
    BC_BIN,    /* r[a] = r[b] op r[c] */
    BC_UN,     /* r[a] = op r[b]      */
    BC_LOAD,   /* r[a] = slot[b]      */
    BC_STORE,  /* slot[a] = r[b]      */
    BC_JMP,    /* pc = a              */
    BC_JT,     /* if r[b] then pc = a */
    BC_JF,     /* if !r[b] then pc = a */
    BC_JREL,   /* if r[b] op r[c] then pc = a */
    BC_FIELD,  /* r[a] = flight(r[b]).field(aux)        */
    BC_BI,     /* r[a] = builtin aux (r[b], r[c])       */
    BC_ENTER,  /* aircraft a enters, state from argbuf  */
    BC_CLEAR,  /* clear flight r[b], kind aux, to r[c]  */
    BC_ADV,    /* advance r[b] seconds                  */
    BC_ASSERT, /* assert r[b], message aux              */
    BC_PRINT,  /* print r[b] as type aux                */
    BC_PRINTS, /* print string aux                      */
    BC_ARG,    /* argbuf[a] = r[b]                      */
    BC_CALL,   /* call function a with b arguments      */
    BC_RET,
    BC_HALT
} BcOp;

const char *bc_op_name(BcOp op);

typedef struct Bc {
    BcOp op;
    BinOp bop; /* BC_BIN, BC_UN, BC_JREL */
    int aux;   /* field, built-in, clearance kind, string index, print type */
    int a, b, c;
    double k;
    int line, col;
} Bc;

/* Computes next-use information, allocates registers and emits bytecode.
 * `is_main` makes a return halt the machine rather than pop a frame. */
void codegen_run(Func *fn, OptStats *stats, int is_main);
void codegen_run_all(FuncList *fns, OptStats *stats);
void bc_print(FILE *f, const Program *p, const Func *fn);
void bc_print_all(FILE *f, const Program *p, const FuncList *fns);
/* Next-use distances for the IR instruction at index i, for the UI. */
void nextuse_print(FILE *f, const Func *fn);

/* ------------------------------------------------------------------ *
 * AeroVM: simulation and verification  (src/vm.c, SPEC section 12)
 * ------------------------------------------------------------------ */

typedef struct {
    Str name;
    double lat, lon;
    double alt, hdg, spd;          /* current state: ft, degrees true, knots */
    double t_alt, t_hdg, t_spd;    /* commanded targets */
    int direct_fix;                /* fix index being tracked, or -1 */
    int active;
} FlightState;

typedef enum { EV_CLEAR, EV_ENTER, EV_LOS, EV_ASSERT_OK, EV_ASSERT_FAIL, EV_PRINT } EventKind;

typedef struct {
    EventKind kind;
    double t;        /* simulation time, seconds */
    int flight, other;
    ClearKind clear;
    double value;
    int line, col;
    char text[256];
} Event;

typedef struct {
    Event *items;
    int count, cap;
} EventList;

/* A position sample, so a demonstration can draw what actually happened. */
typedef struct {
    int flight;
    double t, lat, lon, alt;
} TrackPoint;

typedef struct {
    TrackPoint *items;
    int count, cap;
} TrackList;

typedef struct {
    double t;            /* final simulation time */
    EventList events;
    TrackList track;     /* positions sampled through the run */
    int steps;           /* bytecode instructions executed */
    int los_count;       /* losses of separation observed */
    int asserts_run, asserts_failed;
    double min_hsep, min_vsep; /* closest approach seen between any pair */
    int min_pair_a, min_pair_b;
    FlightState *flights;
    int nflights;
} VmResult;

/* Separation standard used by the monitor. Prototype values, see SPEC. */
#define SEP_H_NM 5.0
#define SEP_V_FT 1000.0

void vm_run(const Program *p, const FuncList *fns, VmResult *out, DiagList *diags);
void vm_result_free(VmResult *r);
void vm_print_trace(FILE *f, const Program *p, const VmResult *r);

/* ------------------------------------------------------------------ *
 * Scenario-file backend  (src/scn.c, SPEC section 13)
 * ------------------------------------------------------------------ */

void scn_emit(FILE *f, const Program *p, const VmResult *r);
/* Formats an altitude the way a scenario file writes it: a flight level
 * where the value is a round hundred feet, and plain feet otherwise. */
void scn_format_alt(double ft, char *buf, size_t n);

/* ------------------------------------------------------------------ *
 * Driver  (src/compile.c, src/json.c, src/main.c, src/server.c)
 * ------------------------------------------------------------------ */

typedef struct {
    int optimise;   /* run the optimiser (default 1) */
    int simulate;   /* run the AeroVM  (default 1)   */
    int trace;      /* print a VM trace              */
} Options;

/* A whole compilation. The source buffer passed to unit_compile must stay
 * alive for as long as the Unit does: every name in the AST and the symbol
 * table is a view into it, not a copy. */
typedef struct {
    Program prog;
    SymTab st;
    TokenList tokens;
    DiagList diags;
    FuncList funcs;     /* unoptimised IR, kept for the UI */
    FuncList opt_funcs; /* optimised IR + bytecode */
    OptStats stats;
    VmResult vm;
    int lexed, parsed, checked, lowered, ran;
} Unit;

Options options_default(void);
/* Runs as much of the pipeline as succeeds. Always safe to unit_free after. */
void unit_compile(Unit *u, const char *src, Options opt);
void unit_free(Unit *u);

void json_dump(FILE *out, const Unit *u, const char *src);

char *read_all(FILE *f);
int serve(int port);

/* Portability (src/port.c): an in-memory output stream and strcasestr. */
typedef struct {
    FILE *f;
    char *buf;
    size_t len;
    char path[512];
} MemOut;
FILE *memout_open(MemOut *m);
/* Closes the stream; returns its contents NUL-terminated (caller frees). */
char *memout_close(MemOut *m, size_t *len);
const char *str_find_ci(const char *hay, const char *needle);

#endif /* AERODSL_H */
