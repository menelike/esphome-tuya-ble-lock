// Self-contained MD5 + AES-128-CBC for the HOST unit-test build only.
// The on-device build uses ESP-IDF's mbedTLS instead (tuya_protocol.cpp includes
// <mbedtls/md5.h> / <mbedtls/aes.h>). This file provides those same symbols so the
// protocol code can be linked and tested on a host with no external crypto dependency.
//
// MD5: public-domain implementation (RFC 1321 style). AES-128: compact public-domain
// implementation (tiny-AES style), CBC mode.

#include <cstdint>
#include <cstring>
#include <cstddef>

// ================= MD5 =================
namespace {
struct MD5Ctx { uint32_t a, b, c, d; uint64_t len; uint8_t buf[64]; size_t buflen; };

inline uint32_t rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

void md5_block(MD5Ctx *ctx, const uint8_t *p) {
  static const uint32_t K[64] = {
      0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
      0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
      0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
      0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
      0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
      0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
      0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
      0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
  static const int S[64] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
                            5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
                            4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
                            6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
  uint32_t M[16];
  for (int i = 0; i < 16; i++)
    M[i] = p[i*4] | (p[i*4+1]<<8) | (p[i*4+2]<<16) | ((uint32_t)p[i*4+3]<<24);
  uint32_t A=ctx->a,B=ctx->b,C=ctx->c,D=ctx->d;
  for (int i = 0; i < 64; i++) {
    uint32_t F; int g;
    if (i < 16) { F=(B&C)|(~B&D); g=i; }
    else if (i < 32) { F=(D&B)|(~D&C); g=(5*i+1)&15; }
    else if (i < 48) { F=B^C^D; g=(3*i+5)&15; }
    else { F=C^(B|~D); g=(7*i)&15; }
    F += A + K[i] + M[g];
    A=D; D=C; C=B; B += rol(F, S[i]);
  }
  ctx->a+=A; ctx->b+=B; ctx->c+=C; ctx->d+=D;
}
void md5_init(MD5Ctx *c){ c->a=0x67452301;c->b=0xefcdab89;c->c=0x98badcfe;c->d=0x10325476;c->len=0;c->buflen=0; }
void md5_update(MD5Ctx *c,const uint8_t *d,size_t n){
  c->len+=n;
  while(n){ size_t t=64-c->buflen; if(t>n)t=n; memcpy(c->buf+c->buflen,d,t); c->buflen+=t; d+=t; n-=t;
    if(c->buflen==64){ md5_block(c,c->buf); c->buflen=0; } }
}
void md5_final(MD5Ctx *c,uint8_t out[16]){
  uint64_t bits=c->len*8; uint8_t pad=0x80; md5_update(c,&pad,1);
  uint8_t z=0; while(c->buflen!=56) md5_update(c,&z,1);
  uint8_t lb[8]; for(int i=0;i<8;i++) lb[i]=(bits>>(8*i))&0xFF; md5_update(c,lb,8);
  uint32_t v[4]={c->a,c->b,c->c,c->d};
  for(int i=0;i<4;i++){ out[i*4]=v[i]&0xFF; out[i*4+1]=(v[i]>>8)&0xFF; out[i*4+2]=(v[i]>>16)&0xFF; out[i*4+3]=(v[i]>>24)&0xFF; }
}
}  // namespace

// mbedTLS-compatible MD5 shims. The struct comes from the stub <mbedtls/md5.h> (opaque
// byte buffer); we reinterpret its storage as our MD5Ctx.
#include "mbedtls/md5.h"
static_assert(sizeof(MD5Ctx) <= sizeof(mbedtls_md5_context), "md5 ctx too big");
extern "C" {
void mbedtls_md5_init(mbedtls_md5_context *x){ (void)x; }
void mbedtls_md5_free(mbedtls_md5_context *x){ (void)x; }
int mbedtls_md5_starts(mbedtls_md5_context *x){ md5_init(reinterpret_cast<MD5Ctx*>(x)); return 0; }
int mbedtls_md5_update(mbedtls_md5_context *x,const unsigned char *d,size_t n){ md5_update(reinterpret_cast<MD5Ctx*>(x),d,n); return 0; }
int mbedtls_md5_finish(mbedtls_md5_context *x,unsigned char out[16]){ md5_final(reinterpret_cast<MD5Ctx*>(x),out); return 0; }
}

// ================= AES-128 (tiny, public domain) =================
namespace {
const uint8_t sbox[256]={
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
uint8_t rsbox[256];
uint8_t xtime(uint8_t x){ return (x<<1) ^ ((x>>7)*0x1b); }
uint8_t mul(uint8_t a,uint8_t b){ uint8_t r=0; for(int i=0;i<8;i++){ if(b&1)r^=a; b>>=1; a=xtime(a);} return r; }

struct AES { uint8_t rk[176]; };
void aes_key(AES *a,const uint8_t key[16]){
  memcpy(a->rk,key,16); uint8_t rcon=1;
  for(int i=16;i<176;i+=4){
    uint8_t t[4]; memcpy(t,a->rk+i-4,4);
    if(i%16==0){ uint8_t tmp=t[0]; t[0]=sbox[t[1]]^rcon; t[1]=sbox[t[2]]; t[2]=sbox[t[3]]; t[3]=sbox[tmp]; rcon=xtime(rcon); }
    for(int j=0;j<4;j++) a->rk[i+j]=a->rk[i-16+j]^t[j];
  }
}
void add_rk(uint8_t *s,const uint8_t *rk){ for(int i=0;i<16;i++) s[i]^=rk[i]; }
void sub_s(uint8_t *s){ for(int i=0;i<16;i++) s[i]=sbox[s[i]]; }
void inv_sub(uint8_t *s){ for(int i=0;i<16;i++) s[i]=rsbox[s[i]]; }
void shift(uint8_t *s){ uint8_t t;
  t=s[1];s[1]=s[5];s[5]=s[9];s[9]=s[13];s[13]=t;
  t=s[2];s[2]=s[10];s[10]=t; t=s[6];s[6]=s[14];s[14]=t;
  t=s[15];s[15]=s[11];s[11]=s[7];s[7]=s[3];s[3]=t; }
void inv_shift(uint8_t *s){ uint8_t t;
  t=s[13];s[13]=s[9];s[9]=s[5];s[5]=s[1];s[1]=t;
  t=s[2];s[2]=s[10];s[10]=t; t=s[6];s[6]=s[14];s[14]=t;
  t=s[3];s[3]=s[7];s[7]=s[11];s[11]=s[15];s[15]=t; }
void mix(uint8_t *s){ for(int i=0;i<4;i++){ uint8_t *c=s+i*4,a0=c[0],a1=c[1],a2=c[2],a3=c[3];
  c[0]=xtime(a0)^(xtime(a1)^a1)^a2^a3; c[1]=a0^xtime(a1)^(xtime(a2)^a2)^a3;
  c[2]=a0^a1^xtime(a2)^(xtime(a3)^a3); c[3]=(xtime(a0)^a0)^a1^a2^xtime(a3); } }
void inv_mix(uint8_t *s){ for(int i=0;i<4;i++){ uint8_t *c=s+i*4,a0=c[0],a1=c[1],a2=c[2],a3=c[3];
  c[0]=mul(a0,14)^mul(a1,11)^mul(a2,13)^mul(a3,9); c[1]=mul(a0,9)^mul(a1,14)^mul(a2,11)^mul(a3,13);
  c[2]=mul(a0,13)^mul(a1,9)^mul(a2,14)^mul(a3,11); c[3]=mul(a0,11)^mul(a1,13)^mul(a2,9)^mul(a3,14); } }
void enc_block(AES *a,uint8_t *s){ add_rk(s,a->rk); for(int r=1;r<10;r++){ sub_s(s);shift(s);mix(s);add_rk(s,a->rk+r*16);} sub_s(s);shift(s);add_rk(s,a->rk+160); }
void dec_block(AES *a,uint8_t *s){ add_rk(s,a->rk+160); for(int r=9;r>0;r--){ inv_shift(s);inv_sub(s);add_rk(s,a->rk+r*16);inv_mix(s);} inv_shift(s);inv_sub(s);add_rk(s,a->rk); }
struct RsboxInit { RsboxInit(){ for(int i=0;i<256;i++) rsbox[sbox[i]]=i; } } _rinit;
}  // namespace

// mbedTLS-compatible AES shims (CBC subset). Struct from stub <mbedtls/aes.h>.
#include "mbedtls/aes.h"
static_assert(sizeof(AES) <= sizeof(mbedtls_aes_context), "aes ctx too big");
extern "C" {
void mbedtls_aes_init(mbedtls_aes_context *x){ (void)x; }
void mbedtls_aes_free(mbedtls_aes_context *x){ (void)x; }
int mbedtls_aes_setkey_enc(mbedtls_aes_context *x,const unsigned char *k,unsigned){ aes_key(reinterpret_cast<AES*>(x),k); return 0; }
int mbedtls_aes_setkey_dec(mbedtls_aes_context *x,const unsigned char *k,unsigned){ aes_key(reinterpret_cast<AES*>(x),k); return 0; }
int mbedtls_aes_crypt_cbc(mbedtls_aes_context *x,int mode,size_t len,unsigned char iv[16],
                          const unsigned char *in,unsigned char *out){
  AES *a=reinterpret_cast<AES*>(x);
  uint8_t block[16];
  for(size_t o=0;o<len;o+=16){
    if(mode==MBEDTLS_AES_ENCRYPT){ for(int i=0;i<16;i++) block[i]=in[o+i]^iv[i]; enc_block(a,block); memcpy(out+o,block,16); memcpy(iv,block,16); }
    else { uint8_t civ[16]; memcpy(civ,in+o,16); memcpy(block,in+o,16); dec_block(a,block); for(int i=0;i<16;i++) out[o+i]=block[i]^iv[i]; memcpy(iv,civ,16); }
  }
  return 0;
}
}
