#pragma once
#include <cstddef>
#define MBEDTLS_AES_ENCRYPT 1
#define MBEDTLS_AES_DECRYPT 0
struct mbedtls_aes_context { unsigned char _[256]; };
extern "C" {
void mbedtls_aes_init(mbedtls_aes_context *);
void mbedtls_aes_free(mbedtls_aes_context *);
int mbedtls_aes_setkey_enc(mbedtls_aes_context *, const unsigned char *, unsigned);
int mbedtls_aes_setkey_dec(mbedtls_aes_context *, const unsigned char *, unsigned);
int mbedtls_aes_crypt_cbc(mbedtls_aes_context *, int, size_t, unsigned char[16],
                          const unsigned char *, unsigned char *);
}
