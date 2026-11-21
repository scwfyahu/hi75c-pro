#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "expr.h"
int main(void) {
    int fails = 0;
    /* deep unary chain: used to SIGSEGV inside expr_compile */
    {
        char *buf = malloc(5003);
        int i;
        for (i = 0; i < 5000; i++) buf[i] = '-';
        buf[5000] = '1'; buf[5001] = 0;
        char err[64] = "";
        Expr *e = expr_compile(buf, err, sizeof(err));
        if (e) { printf("FAIL deep unary accepted\n"); expr_free(e); fails++; }
        else printf("ok   deep unary rejected (%s)\n", err);
        free(buf);
    }
    /* deep power chain */
    {
        char *buf = malloc(10003);
        int i, n = 0;
        for (i = 0; i < 2000; i++) { buf[n++] = '1'; buf[n++] = '^'; }
        buf[n++] = '1'; buf[n] = 0;
        char err[64] = "";
        Expr *e = expr_compile(buf, err, sizeof(err));
        if (e) { printf("FAIL deep power accepted\n"); expr_free(e); fails++; }
        else printf("ok   deep power rejected (%s)\n", err);
        free(buf);
    }
    /* a flat sum must still work */
    {
        char *buf = malloc(3002);
        int i, n = 0;
        for (i = 0; i < 1000; i++) { buf[n++] = '1'; buf[n++] = '+'; }
        buf[n++] = '1'; buf[n] = 0;
        char err[64] = "";
        Expr *e = expr_compile(buf, err, sizeof(err));
        if (!e) { printf("FAIL flat sum rejected (%s)\n", err); fails++; }
        else { printf("ok   flat sum accepted = %g\n", expr_eval(e, 0,0,0,0,0)); expr_free(e); }
        free(buf);
    }
    printf("\n%d failures\n", fails);
    return fails ? 1 : 0;
}
