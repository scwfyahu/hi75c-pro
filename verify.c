#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "verify.h"
#include "keys/pubkey.h"

int h75c_verify_with_sig(const wchar_t *exe, const unsigned char *sig, unsigned long siglen) {
    BCRYPT_ALG_HANDLE alg = NULL, rsa = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    unsigned char digest[32], buf[65536];
    HANDLE f; DWORD got = 0; int ok = 0;

    if (!sig || siglen != 256) return 0;
    f = CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) goto done;
    if (BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) != 0) goto done;
    while (ReadFile(f, buf, sizeof(buf), &got, NULL) && got > 0)
        if (BCryptHashData(hash, buf, got, 0) != 0) goto done;
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0) goto done;
    if (BCryptOpenAlgorithmProvider(&rsa, BCRYPT_RSA_ALGORITHM, NULL, 0) != 0) goto done;
    if (BCryptImportKeyPair(rsa, NULL, BCRYPT_RSAPUBLIC_BLOB, &key,
                            (PUCHAR)H75C_PUBKEY, sizeof(H75C_PUBKEY), 0) != 0) goto done;
    {
        BCRYPT_PKCS1_PADDING_INFO pi;
        pi.pszAlgId = BCRYPT_SHA256_ALGORITHM;   /* PKCS#1 needs the hash named */
        ok = (BCryptVerifySignature(key, &pi, digest, sizeof(digest),
                                    (PUCHAR)sig, siglen, BCRYPT_PAD_PKCS1) == 0);
    }
done:
    if (key) BCryptDestroyKey(key);
    if (rsa) BCryptCloseAlgorithmProvider(rsa, 0);
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return ok;
}

int h75c_verify_signed_file(const wchar_t *exe) {
    wchar_t sig[MAX_PATH + 8];
    unsigned char buf[512]; DWORD got = 0; HANDLE f; int ok;
    lstrcpynW(sig, exe, MAX_PATH);
    lstrcatW(sig, L".sig");
    f = CreateFileW(sig, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    ok = ReadFile(f, buf, sizeof(buf), &got, NULL) ? h75c_verify_with_sig(exe, buf, got) : 0;
    CloseHandle(f);
    return ok;
}
