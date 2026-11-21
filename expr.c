#include "expr.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Node Node;
struct Node {
    int kind;            /* 0 num, 1 var, 2 binop, 3 unary, 4 call, 5 ternary */
    double num;
    int op;
    char name[16];
    Node *a, *b, *c;
};

struct Expr { Node *root; };

static const char *P;
static int g_depth;

static Node *parse_expr(void);

static void skip(void) { while (*P == ' ' || *P == '\t') P++; }

static Node *node(int kind) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    if (n) n->kind = kind;
    return n;
}


/* arity of each callable name: a wrong count used to be filled with zeros and
   silently change the result (clamp(5) made every key black) */

/* only these names are variables; anything else is a typo that used to
   evaluate to 0 and silently change the result */
static int is_variable(const char *n) {
    return !strcmp(n, "t") || !strcmp(n, "x") || !strcmp(n, "y") ||
           !strcmp(n, "i") || !strcmp(n, "n") || !strcmp(n, "pi") ||
           !strcmp(n, "tau") || !strcmp(n, "e");
}

static int arity_of(const char *name) {
    static const struct { const char *n; int a; } fns[] = {
        {"sin",1},{"cos",1},{"tan",1},{"abs",1},{"floor",1},{"ceil",1},
        {"round",1},{"sqrt",1},{"log",1},{"exp",1},{"fract",1},{"tri",1},
        {"pow",2},{"min",2},{"max",2},{"mod",2},{"step",2},
        {"clamp",3},{"mix",3},{"smoothstep",3}
    };
    int i;
    for (i = 0; i < (int)(sizeof(fns)/sizeof(fns[0])); i++)
        if (!strcmp(name, fns[i].n)) return fns[i].a;
    return -1;                       /* unknown name */
}

static Node *parse_bin(int minprec);
static Node *parse_primary(void) {
    skip();
    if (*P == '(') {
        Node *e;
        if (++g_depth > 64) { g_depth--; return NULL; }   /* pathological nesting */
        P++;
        e = parse_expr();
        g_depth--;
        skip();
        if (*P != ')') return NULL;      /* an unclosed group is a syntax error */
        P++;
        return e;
    }
    if (*P == '-') {
        Node *n;
        P++;
        n = node(3);
        if (!n) return NULL;
        n->op = '-';
        /* exponent binds tighter than the sign, so -2^2 is -(2^2) */
        n->a = parse_bin(6);
        if (!n->a) return NULL;
        return n;
    }
    if (isdigit((unsigned char)*P) || *P == '.') {
        char *end = NULL;
        double v = strtod(P, &end);
        Node *n = node(0);
        if (!n || end == P) return NULL;     /* allocation failure or no digits */
        n->num = v;
        P = end;
        return n;
    }
    if (isalpha((unsigned char)*P) || *P == '_') {
        char name[16];
        int len = 0;
        while ((isalnum((unsigned char)*P) || *P == '_') && len < 15) name[len++] = *P++;
        name[len] = 0;
        skip();
        if (*P == '(') {
            Node *n = node(4);
            int argc;
            if (!n) return NULL;
            strncpy(n->name, name, 15);
            P++;
            skip();
            argc = 0;
            if (*P != ')') {                 /* a call needs its closing paren */
                n->a = parse_expr();
                argc = 1;
                skip();
                if (*P == ',') { P++; skip(); n->b = parse_expr(); argc = 2; skip(); }
                if (*P == ',') { P++; skip(); n->c = parse_expr(); argc = 3; skip(); }
            }
            if (*P != ')') return NULL;
            P++;
            {
                int want = arity_of(name);
                if (want < 0) return NULL;              /* unknown function */
                if (want != argc) return NULL;          /* wrong argument count */
            }
            if (!n->a) return NULL;
            return n;
        }
        {
            Node *n;
            if (!is_variable(name)) return NULL;   /* unknown name, not a variable */
            n = node(1);
            if (!n) return NULL;
            strncpy(n->name, name, 15);
            return n;
        }
    }
    return NULL;
}

static int precedence(int op) {
    switch (op) {
        case 1: return 1;   /* || */
        case 2: return 2;   /* && */
        case 3: case 4: return 3;  /* < > */
        case 5: case 6: return 4;  /* + - */
        case 7: case 8: return 5;  /* * / */
        case 9: return 6;   /* ^ */
    }
    return 0;
}

static int peek_op(void) {
    skip();
    if (P[0] == '|' && P[1] == '|') return 1;
    if (P[0] == '&' && P[1] == '&') return 2;
    if (P[0] == '<') return 3;
    if (P[0] == '>') return 4;
    if (P[0] == '+') return 5;
    if (P[0] == '-') return 6;
    if (P[0] == '*') return 7;
    if (P[0] == '/') return 8;
    if (P[0] == '^') return 9;
    return 0;
}

static void take_op(int op) {
    if (op == 1 || op == 2) P += 2;
    else P += 1;
}

static Node *parse_bin(int minprec) {
    Node *left = parse_primary();
    for (;;) {
        int op = peek_op();
        int prec;
        if (!op) break;
        prec = precedence(op);
        if (prec < minprec) break;
        take_op(op);
        {
            Node *right = parse_bin(op == 9 ? prec : prec + 1);   /* 9 = right assoc */
            Node *n;
            if (!left || !right) return NULL;   /* a half-parsed expression is an error */
            n = node(2);
            if (!n) return NULL;
            n->op = op;
            n->a = left;
            n->b = right;
            left = n;
        }
    }
    return left;
}

static Node *parse_expr(void) {
    Node *cond = parse_bin(0);
    skip();
    if (*P == '?') {
        Node *n = node(5);
        P++;
        if (!n) return NULL;
        n->a = cond;
        n->b = parse_expr();
        skip();
        if (*P != ':') return NULL;      /* '?:' needs both halves */
        P++;
        n->c = parse_expr();
        if (!n->a || !n->b || !n->c) return NULL;
        return n;
    }
    return cond;
}

static void node_free(Node *n);

Expr *expr_compile(const char *text, char *err, int errlen) {
    Expr *e = (Expr *)calloc(1, sizeof(Expr));
    if (!e) return NULL;
    P = text;
    g_depth = 0;
    e->root = parse_expr();
    skip();
    if (!e->root) {
        if (err) snprintf(err, errlen, "could not parse the expression");
        free(e);
        return NULL;
    }
    if (*P) {                     /* trailing junk means the formula is wrong */
        node_free(e->root);
        free(e);
        if (err) snprintf(err, errlen, "unexpected text at %.16s", P);
        return NULL;
    }
    if (err && errlen > 0) err[0] = 0;
    return e;
}

static double call(const char *name, double a, double b, double c) {
    if (!strcmp(name, "sin")) return sin(a);
    if (!strcmp(name, "cos")) return cos(a);
    if (!strcmp(name, "tan")) return tan(a);
    if (!strcmp(name, "abs")) return fabs(a);
    if (!strcmp(name, "floor")) return floor(a);
    if (!strcmp(name, "ceil")) return ceil(a);
    if (!strcmp(name, "round")) return floor(a + 0.5);
    if (!strcmp(name, "sqrt")) return a > 0 ? sqrt(a) : 0;
    if (!strcmp(name, "pow")) return pow(a, b);
    if (!strcmp(name, "log")) return a > 0 ? log(a) : 0;
    if (!strcmp(name, "exp")) return exp(a);
    if (!strcmp(name, "min")) return a < b ? a : b;
    if (!strcmp(name, "max")) return a > b ? a : b;
    if (!strcmp(name, "mod")) return b ? fmod(a, b) : 0;
    if (!strcmp(name, "clamp")) return a < b ? b : (a > c ? c : a);
    if (!strcmp(name, "mix")) return a + (b - a) * c;
    if (!strcmp(name, "step")) return a <= b ? 1 : 0;
    if (!strcmp(name, "smoothstep")) {          /* smoothstep(edge0, edge1, x) */
        double t = (b == a) ? 0 : (c - a) / (b - a);
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        return t * t * (3 - 2 * t);
    }
    if (!strcmp(name, "fract")) return a - floor(a);
    if (!strcmp(name, "tri")) { double f = a - floor(a); return f < 0.5 ? f * 2 : 2 - f * 2; }
    return 0;
}

static void node_free(Node *n) {
    if (!n) return;
    node_free(n->a);
    node_free(n->b);
    node_free(n->c);
    free(n);
}

static double eval(Node *n, double t, double x, double y, double i, double cnt) {
    if (!n) return 0;
    switch (n->kind) {
        case 0: return n->num;
        case 1:
            if (!strcmp(n->name, "t")) return t;
            if (!strcmp(n->name, "x")) return x;
            if (!strcmp(n->name, "y")) return y;
            if (!strcmp(n->name, "i")) return i;
            if (!strcmp(n->name, "n")) return cnt;
            if (!strcmp(n->name, "pi")) return 3.14159265358979;
            if (!strcmp(n->name, "tau")) return 6.28318530717959;
            if (!strcmp(n->name, "e")) return 2.71828182845905;
            return 0;
        case 2: {
            double a = eval(n->a, t, x, y, i, cnt), b = eval(n->b, t, x, y, i, cnt);
            switch (n->op) {
                case 1: return (a != 0 || b != 0) ? 1 : 0;
                case 2: return (a != 0 && b != 0) ? 1 : 0;
                case 3: return a < b;
                case 4: return a > b;
                case 5: return a + b;
                case 6: return a - b;
                case 7: return a * b;
                case 8: return b != 0 ? a / b : 0;
                case 9: return pow(a, b);
            }
            return 0;
        }
        case 3: return -eval(n->a, t, x, y, i, cnt);
        case 4:
            return call(n->name, eval(n->a, t, x, y, i, cnt),
                        n->b ? eval(n->b, t, x, y, i, cnt) : 0,
                        n->c ? eval(n->c, t, x, y, i, cnt) : 0);
        case 5:
            return eval(n->a, t, x, y, i, cnt) != 0 ? eval(n->b, t, x, y, i, cnt)
                                                    : eval(n->c, t, x, y, i, cnt);
    }
    return 0;
}

double expr_eval(Expr *e, double t, double x, double y, double i, double n) {
    return e ? eval(e->root, t, x, y, i, n) : 0;
}

void expr_free(Expr *e) {
    if (!e) return;
    node_free(e->root);
    free(e);
}
