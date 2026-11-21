#include <stdio.h>
#include "expr.h"
int main(void) {
    struct { const char *e; double want; } t[] = {
        {"-2^2", -4}, {"0-2^2", -4}, {"-(2^2)", -4}, {"-2^-2", -0.25},
        {"2^3^2", 512}, {"-2*3", -6}, {"3-2^2", -1}, {"(-2)^2", 4},
    };
    int i, fails = 0;
    for (i = 0; i < 8; i++) {
        char err[64] = "";
        Expr *e = expr_compile(t[i].e, err, sizeof(err));
        if (!e) { printf("FAIL %s did not compile (%s)\n", t[i].e, err); fails++; continue; }
        double v = expr_eval(e, 0, 0, 0, 0, 0);
        if (v < t[i].want - 1e-9 || v > t[i].want + 1e-9) {
            printf("FAIL %-10s = %g, want %g\n", t[i].e, v, t[i].want); fails++;
        } else printf("ok   %-10s = %g\n", t[i].e, v);
        expr_free(e);
    }
    printf("\n%d failures\n", fails);
    return fails ? 1 : 0;
}
