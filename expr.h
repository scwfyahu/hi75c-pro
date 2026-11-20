/* tiny expression engine for effect files: one compiled expression, re-evaluated per key per frame */
#ifndef EXPR_H
#define EXPR_H

typedef struct Expr Expr;

Expr *expr_compile(const char *text, char *err, int errlen);
double expr_eval(Expr *e, double t, double x, double y, double i, double n);
void expr_free(Expr *e);

#endif
