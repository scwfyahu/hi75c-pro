#include <stdio.h>
#include "expr.h"
int main(void) {
    const char *bad[] = {"clamp(5)", "mix(0,100)", "sin()", "pow(2)", "min(1,2,3)",
                         "clamp(1,2,3,4)", "nope(5)", "sin", "sin(1,2)"};
    const char *good[] = {"sin(t)", "clamp(t*255,0,255)", "mix(0,255,t)", "pow(2,3)",
                          "min(1,2)", "step(0.5,t)", "smoothstep(0,1,t)"};
    int i, fails = 0, checks = 0;
    for (i = 0; i < (int)(sizeof(bad)/sizeof(bad[0])); i++) {
        char err[64] = ""; Expr *e = expr_compile(bad[i], err, sizeof(err)); checks++;
        if (e) { printf("FAIL accepted %s\n", bad[i]); expr_free(e); fails++; }
        else printf("ok   rejected %s\n", bad[i]);
    }
    for (i = 0; i < (int)(sizeof(good)/sizeof(good[0])); i++) {
        char err[64] = ""; Expr *e = expr_compile(good[i], err, sizeof(err)); checks++;
        if (!e) { printf("FAIL rejected valid %s (%s)\n", good[i], err); fails++; }
        else { printf("ok   accepted %s\n", good[i]); expr_free(e); }
    }
    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
