#include <stdio.h>
#include "expr.h"
int main(void) {
    const char *bad[] = {"1+", "*3", "sin(", "(1+2", "1+2)", "1 2", "1+2 extra",
                         "1?2", "?", "-", "5%3", "t==1", "t>=0.5", "t<=0.5", ""};
    const char *good[] = {"1", "t*2", "clamp(t*255,0,255)", "sin(t)*127+128",
                          "-(2^2)", "2^3^2", "t > 0.5 ? 255 : 0"};
    int i, fails = 0, checks = 0;
    for (i = 0; i < (int)(sizeof(bad)/sizeof(bad[0])); i++) {
        char err[64] = "";
        Expr *e = expr_compile(bad[i], err, sizeof(err));
        checks++;
        if (e) { printf("FAIL accepted \"%s\"\n", bad[i]); expr_free(e); fails++; }
        else printf("ok   rejected \"%s\"  (%s)\n", bad[i], err);
    }
    for (i = 0; i < (int)(sizeof(good)/sizeof(good[0])); i++) {
        char err[64] = "";
        Expr *e = expr_compile(good[i], err, sizeof(err));
        checks++;
        if (!e) { printf("FAIL rejected valid \"%s\" (%s)\n", good[i], err); fails++; }
        else { printf("ok   accepted \"%s\"\n", good[i]); expr_free(e); }
    }
    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
