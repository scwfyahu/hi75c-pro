/* version comparison used by the updater; a wrong answer silently ships a
   stale build or nags about an update that does not exist */
#include <stdio.h>

static int fails, checks;

static int version_gt(const char *a, const char *b) {
    while (*a && (*a < '0' || *a > '9')) a++;
    while (*b && (*b < '0' || *b > '9')) b++;
    for (;;) {
        long x = 0, y = 0;
        while (*a >= '0' && *a <= '9') x = x * 10 + (*a++ - '0');
        while (*b >= '0' && *b <= '9') y = y * 10 + (*b++ - '0');
        if (x != y) return x > y;
        if (*a != '.' && *b != '.') break;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

static void ok(int cond, const char *what) {
    checks++;
    if (!cond) { fails++; printf("FAIL %s\n", what); }
    else printf("ok   %s\n", what);
}

int main(void) {
    ok(version_gt("v1.0.1", "1.0.0") == 1, "patch bump is newer");
    ok(version_gt("1.0.0", "1.0.0") == 0, "equal is not newer");
    ok(version_gt("1.2", "1.10") == 0, "numeric, not lexical, compare");
    ok(version_gt("1.10", "1.2") == 1, "numeric compare, other direction");
    ok(version_gt("1.0", "1.0.1") == 0, "shorter tag is older");
    ok(version_gt("1.0.0.1", "1.0") == 1, "extra component makes it newer");
    ok(version_gt("release-2.1", "2.0") == 1, "non-numeric prefix ignored");
    ok(version_gt("", "0") == 0, "empty tag handled");
    ok(version_gt("v13.0.0", "9.9.9") == 1, "double digit major");
    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
