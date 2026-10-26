/*
 * AeroDSL lexical analyser (SPEC section 3).
 *
 * Scans the source once, left to right, with no backtracking. A word -- a
 * maximal run of [A-Za-z0-9_] -- is scanned whole and then classified by the
 * priority list in SPEC 3.4, which is what resolves FL320 (a flight level)
 * against AI202 (a callsign) without lookahead.
 *
 * Every lexical error is reported; the driver stops before parsing if any
 * were, because a token stream with holes produces meaningless cascades.
 */
#include "aerodsl.h"

/* ---- keyword table ------------------------------------------------ */

typedef struct {
    const char *word;
    TokenType type;
} Keyword;

static const Keyword KEYWORDS[] = {
    {"scenario", T_SCENARIO}, {"fix", T_FIX},        {"flight", T_FLIGHT},
    {"enter", T_ENTER},       {"alt", T_ALT},        {"hdg", T_HDG},
    {"spd", T_SPD},           {"lat", T_LAT},        {"lon", T_LON},
    {"type", T_TYPE},
    {"proc", T_PROC},         {"let", T_LET},
    {"clear", T_CLEAR},       {"climb", T_CLIMB},    {"descend", T_DESCEND},
    {"heading", T_HEADING},   {"speed", T_SPEED_KW}, {"direct", T_DIRECT},
    {"advance", T_ADVANCE},   {"if", T_IF},          {"then", T_THEN},
    {"else", T_ELSE},         {"while", T_WHILE},    {"do", T_DO},
    {"end", T_END},           {"assert", T_ASSERT},  {"call", T_CALL},
    {"print", T_PRINT},       {"return", T_RETURN},  {"and", T_AND},
    {"or", T_OR},             {"not", T_NOT},        {"true", T_TRUE},
    {"false", T_FALSE},       {"num", T_TY_NUM},     {"bool", T_TY_BOOL},
};

static const char *const TOKEN_NAMES[] = {
    "NUMBER", "FLEVEL", "SPEED", "IDENT", "CALLSIGN", "STRING",
    "scenario", "fix", "flight", "type", "enter", "alt", "hdg", "spd", "lat", "lon",
    "proc", "let", "clear", "climb", "descend", "heading", "speed", "direct",
    "advance", "if", "then", "else", "while", "do", "end",
    "assert", "call", "print", "return",
    "and", "or", "not", "true", "false",
    "num", "bool",
    "+", "-", "*", "/", "%",
    "<", "<=", ">", ">=", "==", "!=", "=",
    "(", ")", ",", ".",
    "end of input",
};

const char *token_name(TokenType t) { return TOKEN_NAMES[t]; }

/* ---- character classes -------------------------------------------- */

static int is_lower(char c) { return c >= 'a' && c <= 'z'; }
static int is_upper(char c) { return c >= 'A' && c <= 'Z'; }
static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_alpha(char c) { return is_lower(c) || is_upper(c); }
static int is_word(char c) { return is_alpha(c) || is_digit(c) || c == '_'; }

/* True if every character of w[0, n) satisfies pred. Empty ranges are false. */
static int all(const char *w, int from, int to, int (*pred)(char)) {
    if (from >= to) return 0;
    for (int i = from; i < to; i++)
        if (!pred(w[i])) return 0;
    return 1;
}

static int upper_or_digit(char c) { return is_upper(c) || is_digit(c); }
static int lower_digit_us(char c) { return is_lower(c) || is_digit(c) || c == '_'; }

/* ---- the scanner -------------------------------------------------- */

typedef struct {
    const char *p;
    const char *line_start;
    int line;
    DiagList *diags;
    TokenList *out;
} Lexer;

static int col_of(const Lexer *lx, const char *at) { return (int)(at - lx->line_start) + 1; }

static void lex_error(Lexer *lx, const char *at, const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_add(lx->diags, PH_LEXICAL, SEV_ERROR, lx->line, col_of(lx, at), "%s", msg);
}

static void emit(Lexer *lx, TokenType type, const char *start, int len, double num, int col) {
    PUSH(*lx->out, (Token){type, {start, len}, num, lx->line, col});
}

/* Value of the decimal digits in w[from, to). */
static double digits(const char *w, int from, int to) {
    double v = 0;
    for (int i = from; i < to; i++) v = v * 10 + (w[i] - '0');
    return v;
}

/*
 * Classifies a word (SPEC 3.4). Returns the token type and sets *num for the
 * types that carry a numeric attribute, or -1 with *why set to an explanation.
 */
static int classify(const char *w, int n, double *num, const char **why) {
    for (int i = 0; i < LEN(KEYWORDS); i++)
        if (str_is((Str){w, n}, KEYWORDS[i].word)) return KEYWORDS[i].type;

    /* FL320 -> 32000 ft. Checked before CALLSIGN, so no aircraft may be
     * called FL plus exactly three digits. */
    if (n == 5 && w[0] == 'F' && w[1] == 'L' && all(w, 2, 5, is_digit)) {
        *num = digits(w, 2, 5) * 100;
        return T_FLEVEL;
    }

    /* 280KT -> 280 kt, at most three digits. */
    if (n >= 3 && n <= 5 && w[n - 2] == 'K' && w[n - 1] == 'T') {
        if (!all(w, 0, n - 2, is_digit)) {
            *why = "a speed is digits followed by KT, as in 280KT";
            return -1;
        }
        *num = digits(w, 0, n - 2);
        return T_SPEED;
    }
    if (n > 5 && w[n - 2] == 'K' && w[n - 1] == 'T' && all(w, 0, n - 2, is_digit)) {
        *why = "a speed has at most three digits";
        return -1;
    }

    if (all(w, 0, n, is_digit)) {
        *num = digits(w, 0, n);
        return T_NUMBER;
    }

    if (is_lower(w[0]) && all(w, 0, n, lower_digit_us)) return T_IDENT;
    if (is_upper(w[0]) && all(w, 0, n, upper_or_digit)) return T_CALLSIGN;

    if (w[0] == '_') {
        *why = "a name must start with a lower-case letter";
        return -1;
    }
    *why = "AeroDSL writes names in lower case and callsigns in UPPER CASE, "
           "so a word may not mix them";
    return -1;
}

/* Scans a number that begins with a digit: digits, an optional fractional
 * part, and then any trailing word characters (which make it a speed, or an
 * error). Handled separately from words because '.' is not a word character. */
static void scan_number(Lexer *lx) {
    const char *start = lx->p;
    int col = col_of(lx, start);
    while (is_digit(*lx->p)) lx->p++;

    int fractional = 0;
    if (*lx->p == '.') {
        if (!is_digit(lx->p[1])) {
            lex_error(lx, lx->p, "a decimal point must be followed by a digit");
            lx->p++;
            return;
        }
        fractional = 1;
        lx->p++;
        while (is_digit(*lx->p)) lx->p++;
    }

    const char *tail = lx->p;
    while (is_word(*lx->p)) lx->p++;
    int n = (int)(lx->p - start);

    if (tail == lx->p) { /* no trailing letters: a plain number */
        emit(lx, T_NUMBER, start, n, strtod(start, NULL), col);
        return;
    }
    if (fractional) {
        lex_error(lx, start, "'%.*s' is not a number; a unit suffix cannot follow a decimal point",
                  n, start);
        return;
    }

    double num = 0;
    const char *why = NULL;
    int type = classify(start, n, &num, &why);
    if (type < 0)
        lex_error(lx, start, "'%.*s' is not a valid token: %s", n, start, why);
    else
        emit(lx, (TokenType)type, start, n, num, col);
}

static void scan_word(Lexer *lx) {
    const char *start = lx->p;
    int col = col_of(lx, start);
    while (is_word(*lx->p)) lx->p++;
    int n = (int)(lx->p - start);

    double num = 0;
    const char *why = NULL;
    int type = classify(start, n, &num, &why);
    if (type < 0)
        lex_error(lx, start, "'%.*s' is not a valid token: %s", n, start, why);
    else
        emit(lx, (TokenType)type, start, n, num, col);
}

static void scan_string(Lexer *lx) {
    const char *open = lx->p;
    int col = col_of(lx, open);
    lx->p++; /* the opening quote */
    const char *start = lx->p;
    while (*lx->p && *lx->p != '"' && *lx->p != '\n') lx->p++;
    if (*lx->p != '"') {
        lex_error(lx, open, "unterminated string");
        return;
    }
    emit(lx, T_STRING, start, (int)(lx->p - start), 0, col);
    lx->p++; /* the closing quote */
}

/* Two-character operators, then one-character ones. */
static void scan_operator(Lexer *lx) {
    const char *start = lx->p;
    int col = col_of(lx, start);
    char c = *lx->p, d = lx->p[1];

    struct {
        char a, b;
        TokenType t;
    } two[] = {{'<', '=', T_LE}, {'>', '=', T_GE}, {'=', '=', T_EQEQ}, {'!', '=', T_NE}};
    for (int i = 0; i < LEN(two); i++) {
        if (c == two[i].a && d == two[i].b) {
            lx->p += 2;
            emit(lx, two[i].t, start, 2, 0, col);
            return;
        }
    }

    struct {
        char a;
        TokenType t;
    } one[] = {{'+', T_PLUS},   {'-', T_MINUS},  {'*', T_STAR},   {'/', T_SLASH},
               {'%', T_PERCENT}, {'<', T_LT},     {'>', T_GT},     {'=', T_ASSIGN},
               {'(', T_LPAREN}, {')', T_RPAREN}, {',', T_COMMA},  {'.', T_DOT}};
    for (int i = 0; i < LEN(one); i++) {
        if (c == one[i].a) {
            lx->p++;
            emit(lx, one[i].t, start, 1, 0, col);
            return;
        }
    }

    unsigned char u = (unsigned char)c;
    lx->p++;
    if (c == '!')
        lex_error(lx, start, "'!' is only valid as part of '!='");
    else if (u >= 0x80) {
        while ((*lx->p & 0xC0) == 0x80) lx->p++; /* the rest of the UTF-8 sequence */
        lex_error(lx, start, "unexpected non-ASCII character");
    } else if (u >= 0x20 && u < 0x7F)
        lex_error(lx, start, "unexpected character '%c'", c);
    else
        lex_error(lx, start, "unexpected control character 0x%02X", u);
}

void tokenize(const char *src, TokenList *out, DiagList *diags) {
    Lexer lx = {src, src, 1, diags, out};

    while (*lx.p) {
        char c = *lx.p;
        if (c == '\n') {
            lx.line++;
            lx.line_start = ++lx.p;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            lx.p++;
        } else if (c == '#') {
            while (*lx.p && *lx.p != '\n') lx.p++;
        } else if (is_digit(c)) {
            scan_number(&lx);
        } else if (is_alpha(c) || c == '_') {
            scan_word(&lx);
        } else if (c == '"') {
            scan_string(&lx);
        } else {
            scan_operator(&lx);
        }
    }
    emit(&lx, T_EOF, lx.p, 0, 0, col_of(&lx, lx.p));
}

void tokens_free(TokenList *t) {
    free(t->items);
    t->items = NULL;
    t->count = t->cap = 0;
}
