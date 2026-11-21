#ifndef HI75C_VERIFY_H
#define HI75C_VERIFY_H
#ifdef __cplusplus
extern "C" {
#endif
/* 1 when the file's bytes match its adjacent "<file>.sig" (RSA-2048/SHA-256) */
int h75c_verify_signed_file(const wchar_t *exe);
/* 1 when sig (256 bytes) is a valid signature of the file */
int h75c_verify_with_sig(const wchar_t *exe, const unsigned char *sig, unsigned long siglen);
#ifdef __cplusplus
}
#endif
#endif
