/* Diagnostics: one list of located, phase-tagged, severity-tagged messages. */
#include "aerodsl.h"

static const char *const PHASE_NAMES[] = {"lexical", "syntax", "semantic", "runtime", "verify"};
static const char *const SEV_NAMES[] = {"error", "warning", "note"};

const char *phase_name(Phase p) { return PHASE_NAMES[p]; }
const char *sev_name(Severity s) { return SEV_NAMES[s]; }

void diag_add(DiagList *d, Phase phase, Severity sev, int line, int col, const char *fmt, ...) {
    Diagnostic diag = {phase, sev, line, col, {0}};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(diag.message, sizeof diag.message, fmt, ap);
    va_end(ap);
    PUSH(*d, diag);
}

void diag_print(FILE *f, const Diagnostic *d) {
    if (d->line > 0)
        fprintf(f, "%d:%d: %s %s: %s\n", d->line, d->col, phase_name(d->phase), sev_name(d->sev),
                d->message);
    else
        fprintf(f, "%s %s: %s\n", phase_name(d->phase), sev_name(d->sev), d->message);
}

void diag_print_all(FILE *f, const DiagList *d) {
    for (int i = 0; i < d->count; i++) diag_print(f, &d->items[i]);
}

int diag_errors(const DiagList *d) {
    int n = 0;
    for (int i = 0; i < d->count; i++)
        if (d->items[i].sev == SEV_ERROR) n++;
    return n;
}

int diag_errors_phase(const DiagList *d, Phase p) {
    int n = 0;
    for (int i = 0; i < d->count; i++)
        if (d->items[i].sev == SEV_ERROR && d->items[i].phase == p) n++;
    return n;
}

int diag_has_at(const DiagList *d, int line) {
    for (int i = 0; i < d->count; i++)
        if (d->items[i].line == line) return 1;
    return 0;
}

void diag_sort(DiagList *d) {
    /* Insertion sort: stable, and the list is short. */
    for (int i = 1; i < d->count; i++) {
        Diagnostic cur = d->items[i];
        long long key = cur.line > 0 ? (long long)cur.line * 1000 + cur.col : (long long)1 << 40;
        int j = i - 1;
        while (j >= 0) {
            Diagnostic *o = &d->items[j];
            long long okey = o->line > 0 ? (long long)o->line * 1000 + o->col : (long long)1 << 40;
            if (okey <= key) break;
            d->items[j + 1] = *o;
            j--;
        }
        d->items[j + 1] = cur;
    }
}

void diag_free(DiagList *d) {
    free(d->items);
    d->items = NULL;
    d->count = d->cap = 0;
}
