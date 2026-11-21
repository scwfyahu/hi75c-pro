#include <stdio.h>
#include <windows.h>
#include "verify.h"
int main(int argc, char **argv) {
    wchar_t w[512];
    if (argc < 2) { printf("usage: verify_test <exe-with-.sig>\n"); return 2; }
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, w, 512);
    printf("%s -> %s\n", argv[1], h75c_verify_signed_file(w) ? "PASS" : "FAIL");
    return 0;
}
