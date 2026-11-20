#include <stdio.h>
#include <string.h>
#include "expr.h"
static int fails, checks;
static void ck(const char *e, double t, double x, double y, double want, double tol) {
    char err[64]; Expr *ex = expr_compile(e, err, sizeof err);
    double got;
    checks++;
    if (!ex) { printf("FAIL %-34s parse: %s\n", e, err); fails++; return; }
    got = expr_eval(ex, t, x, y, 0, 80);
    if (got < want - tol || got > want + tol) {
        printf("FAIL %-34s got %.4f want %.4f\n", e, got, want); fails++;
    } else printf("ok   %-34s = %.3f\n", e, got);
    expr_free(ex);
}
int main(void) {
    ck("1+2*3", 0,0,0, 7, 1e-9);
    ck("(1+2)*3", 0,0,0, 9, 1e-9);
    ck("2^10", 0,0,0, 1024, 1e-9);
    ck("-4+10", 0,0,0, 6, 1e-9);
    ck("sin(pi/2)", 0,0,0, 1, 1e-6);
    ck("max(3, 9)", 0,0,0, 9, 1e-9);
    ck("clamp(15, 0, 10)", 0,0,0, 10, 1e-9);
    ck("mix(0, 100, 0.25)", 0,0,0, 25, 1e-9);
    ck("step(0.5, 0.6)", 0,0,0, 1, 1e-9);
    ck("fract(3.75)", 0,0,0, 0.75, 1e-9);
    ck("t*2", 3,0,0, 6, 1e-9);
    ck("x+y", 0,2,5, 7, 1e-9);
    ck("i", 0,0,0, 0, 1e-9);
    ck("n", 0,0,0, 80, 1e-9);
    ck("1 < 2", 0,0,0, 1, 1e-9);
    ck("1 > 2", 0,0,0, 0, 1e-9);
    ck("t > 1 ? 10 : 20", 2,0,0, 10, 1e-9);
    ck("t > 1 ? 10 : 20", 0,0,0, 20, 1e-9);
    ck("smoothstep(0, 1, 0.5)", 0,0,0, 0.5, 1e-6);
    /* a real effect line */
    ck("clamp(128 + 127*sin(t*2 + x*0.35), 0, 255)", 1.0, 4, 2, 128 + 127 * 0.0, 200);
    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
