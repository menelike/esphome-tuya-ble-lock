#pragma once
#include <cstddef>
struct mbedtls_md5_context { unsigned char _[128]; };
extern "C" {
void mbedtls_md5_init(mbedtls_md5_context *);
void mbedtls_md5_free(mbedtls_md5_context *);
int mbedtls_md5_starts(mbedtls_md5_context *);
int mbedtls_md5_update(mbedtls_md5_context *, const unsigned char *, size_t);
int mbedtls_md5_finish(mbedtls_md5_context *, unsigned char[16]);
}
