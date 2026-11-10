#ifndef SHOCK_AUDIO4_HASH_H
#define SHOCK_AUDIO4_HASH_H

/* Small, one-time SHA-256 verifier for user-supplied soundtrack files. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t audio4_rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
static void audio4_block(uint32_t h[8], const unsigned char block[64]) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t w[64], a,b,c,d,e,f,g,j;
    for (unsigned i=0; i<16; ++i) w[i] = (uint32_t)block[4*i]<<24 | (uint32_t)block[4*i+1]<<16 | (uint32_t)block[4*i+2]<<8 | block[4*i+3];
    for (unsigned i=16; i<64; ++i) {
        uint32_t x=w[i-15], y=w[i-2];
        w[i]=(audio4_rotr(y,17)^audio4_rotr(y,19)^(y>>10))+w[i-7]+(audio4_rotr(x,7)^audio4_rotr(x,18)^(x>>3))+w[i-16];
    }
    a=h[0];b=h[1];c=h[2];d=h[3];e=h[4];f=h[5];g=h[6];j=h[7];
    for (unsigned i=0; i<64; ++i) {
        uint32_t s1=audio4_rotr(e,6)^audio4_rotr(e,11)^audio4_rotr(e,25);
        uint32_t t1=j+s1+((e&f)^(~e&g))+k[i]+w[i];
        uint32_t s0=audio4_rotr(a,2)^audio4_rotr(a,13)^audio4_rotr(a,22);
        uint32_t t2=s0+((a&b)^(a&c)^(b&c));
        j=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=j;
}

static int audio4_sha256_file(const char *path, char hex[65]) {
    static const char digits[]="0123456789abcdef";
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    unsigned char block[64];
    uint64_t bytes=0;
    FILE *file=fopen(path,"rb");
    if (!file) return 0;
    size_t n;
    while ((n=fread(block,1,64,file))==64) { audio4_block(h,block); bytes+=64; }
    if (ferror(file)) { fclose(file); return 0; }
    bytes+=n;
    fclose(file);
    block[n++]=0x80;
    if (n>56) { memset(block+n,0,64-n); audio4_block(h,block); n=0; }
    memset(block+n,0,56-n);
    uint64_t bits=bytes*8;
    for (unsigned i=0;i<8;++i) block[63-i]=(unsigned char)(bits>>(8*i));
    audio4_block(h,block);
    for (unsigned i=0;i<8;++i) for (unsigned b=0;b<4;++b) {
        unsigned char v=(unsigned char)(h[i]>>(24-8*b));
        hex[8*i+2*b]=digits[v>>4]; hex[8*i+2*b+1]=digits[v&15];
    }
    hex[64]=0;
    return 1;
}
#endif
