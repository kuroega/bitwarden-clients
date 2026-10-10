#ifndef VAULT_CRYPTO_H
#define VAULT_CRYPTO_H
#include <windows.h>
#include "progress.h"
int vault_encrypt_progress(const wchar_t *input,const wchar_t *output,const wchar_t *password,VaultProgress *progress);
int vault_decrypt_progress(const wchar_t *input,const wchar_t *output,const wchar_t *password,VaultProgress *progress);
int vault_hash_progress(const wchar_t *path,wchar_t hex[65],VaultProgress *progress);
int vault_encrypt(const wchar_t *input, const wchar_t *output, const wchar_t *password);
int vault_decrypt(const wchar_t *input, const wchar_t *output, const wchar_t *password);
int vault_hash(const wchar_t *path, wchar_t hex[65]);
int vault_crypto_selftest(const wchar_t *directory);
#endif
