/*
  @file
  @brief
    This is an implementation of the AES CTR mode.
    Block size can be chosen in aes.h - available choices are AES128, AES192, AES256.

  @note Input string must be in 16-byte blocks. Space padding if needed.
*/
#include <stdint.h>
#include <stddef.h>
#include <cstring>

#define AES_BITS       256       /* 128, 192, 256 */
#define AES_NBLOCK     16        /* Block length in bytes - AES is 128b block only */
#define Nb             4         /* number of columns comprising a state in AES    */
#define MULTIPLY_AS_FN 0

#if AES_BITS==256
#define AES_keyExpSize 240
#define Nk             8         /* number of 32-bit words in key  */
#define Nr             14        /* number of rounds in AES Cipher */
#elif AES_BITS==192
#define AES_keyExpSize 208
#define Nk             6
#define Nr             12
#else
#define AES_keyExpSize 176
#define Nk             4
#define Nr             10
#endif

typedef uint8_t  U8;
typedef uint32_t U32;

struct AES_ctx 
{
    U8 RK[AES_keyExpSize];         // RoundKey
    U8 IV[AES_NBLOCK];             // for CTR only
};

// state - array holding the intermediate results during decryption.
typedef U8 state_t[4][4];
// The lookup-tables are marked const so they can be placed in read-only storage instead of RAM
// The numbers below can be computed dynamically trading ROM for RAM - 
// This can be useful in (embedded) bootloader applications, where ROM is often limited.
static const U8 sbox[256] = {
//    0     1     2     3     4    5     6     7      8    9     A      B    C     D     E     F
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};
// The round constant word array, Rcon[i], contains the values given by 
// x to the power (i-1) being powers of x (x is denoted as {02}) in the field GF(2^8)
static const U8 Rcon[11] = {
    0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};
/*
 * Jordan Goulder points out in PR #12 (https://github.com/kokke/tiny-AES-C/pull/12),
 * that you can remove most of the elements in the Rcon array, because they are unused.
 *
 * From Wikipedia's article on the Rijndael key schedule @ https://en.wikipedia.org/wiki/Rijndael_key_schedule#Rcon
 * 
 * "Only the first some of these constants are actually used – up to rcon[10] for AES-128 (as 11 round keys are needed), 
 *  up to rcon[8] for AES-192, up to rcon[7] for AES-256. rcon[0] is not used in AES algorithm."
 */
///
///> produces Nb(Nr+1) round keys. The round keys are used in each round to decrypt the states.
///
#define SUB(t)       ({ t[0]=sbox[t[0]]; t[1]=sbox[t[1]]; t[2]=sbox[t[2]]; t[3]=sbox[t[3]]; })
#define ROR(t)       ({ U8 t0=t[0]; t[0]=t[1]; t[1]=t[2]; t[2]=t[3]; t[3]=t0; })
#define SET(b, a)    (*(U32*)(b) = *(U32*)(a))
#define XOR(c, b, a) (*(U32*)(c) = *(U32*)(b) ^ *(U32*)(a))

static void KeyExpansion(U8* rk, const U8* key)
{
    U8 tmp[4]; // Used for the column/row operations
  
    // The first round key is the key itself.
    for (int i = 0; i < Nk; ++i) {
        SET(&rk[i*4], &key[i*4]);
    }
    // All other round keys are found from the previous round keys.
    for (int i = Nk; i < Nb * (Nr + 1); ++i) {
        SET(tmp, &rk[(i - 1)*4]);
        
        if (i % Nk == 0) {
            // This function shifts the 4 bytes in a word to the left once.
            // [a0,a1,a2,a3] becomes [a1,a2,a3,a0]
            ROR(tmp);
            SUB(tmp);
            tmp[0] = tmp[0] ^ Rcon[i/Nk];
        }
#if AES_BITS==256
        if (i % Nk == 4) SUB(tmp);
#endif
        XOR(&rk[i * 4], &rk[(i - Nk) * 4], tmp);
    }
}
// This function adds the round key to state.
// The round key is added to the state by an XOR function.
static void AddRoundKey(U8 n, state_t* st, const U8* rk) {
    for (U8 i = 0; i < 4; ++i) {
        for (U8 j = 0; j < 4; ++j) {
            (*st)[i][j] ^= rk[(n * Nb * 4) + (i * Nb) + j];
        }
    }
}

// The SubBytes Function Substitutes the values in the
// state matrix with values in an S-box.
static void SubBytes(state_t* st)
{
    for (U8 i = 0; i < 4; ++i) {
        for (U8 j = 0; j < 4; ++j) {
            (*st)[j][i] = sbox[(*st)[j][i]];
        }
    }
}

// The ShiftRows() function shifts the rows in the state to the left.
// Each row is shifted with different offset.
// Offset = Row number. So the first row is not shifted.
static void ShiftRows(state_t* st)
{
    U8 tmp;

    // Rotate first row 1 columns to left  
    tmp         = (*st)[0][1];
    (*st)[0][1] = (*st)[1][1];
    (*st)[1][1] = (*st)[2][1];
    (*st)[2][1] = (*st)[3][1];
    (*st)[3][1] = tmp;

    // Rotate second row 2 columns to left
    tmp         = (*st)[0][2];
    (*st)[0][2] = (*st)[2][2];
    (*st)[2][2] = tmp;

    tmp         = (*st)[1][2];
    (*st)[1][2] = (*st)[3][2];
    (*st)[3][2] = tmp;

    // Rotate third row 3 columns to left
    tmp         = (*st)[0][3];
    (*st)[0][3] = (*st)[3][3];
    (*st)[3][3] = (*st)[2][3];
    (*st)[2][3] = (*st)[1][3];
    (*st)[1][3] = tmp;
}

static U8 xtime(U8 x)
{
    return ((x<<1) ^ (((x>>7) & 1) * 0x1b));
}

// MixColumns function mixes the columns of the state matrix
static void MixColumns(state_t* st)
{
    for (U8 i = 0; i < 4; ++i) {
        U8 t   = (*st)[i][0];
        U8 tmp = (*st)[i][0] ^ (*st)[i][1] ^ (*st)[i][2] ^ (*st)[i][3] ;
        U8 tm  = (*st)[i][0] ^ (*st)[i][1] ; tm = xtime(tm);  (*st)[i][0] ^= tm ^ tmp ;
           tm  = (*st)[i][1] ^ (*st)[i][2] ; tm = xtime(tm);  (*st)[i][1] ^= tm ^ tmp ;
           tm  = (*st)[i][2] ^ (*st)[i][3] ; tm = xtime(tm);  (*st)[i][2] ^= tm ^ tmp ;
           tm  = (*st)[i][3] ^ t ;           tm = xtime(tm);  (*st)[i][3] ^= tm ^ tmp ;
    }
}

// Multiply is used to multiply numbers in the field GF(2^8)
// Note: The last call to xtime() is unneeded, but often ends up generating a smaller binary
//       The compiler seems to be able to vectorize the operation better this way.
//       See https://github.com/kokke/tiny-AES-c/pull/34
#if MULTIPLY_AS_FN
static U8 Multiply(U8 x, U8 y)
{
    return (((y & 1) * x) ^
            ((y>>1 & 1) * xtime(x)) ^
            ((y>>2 & 1) * xtime(xtime(x))) ^
            ((y>>3 & 1) * xtime(xtime(xtime(x)))) ^
            ((y>>4 & 1) * xtime(xtime(xtime(xtime(x)))))); /* this last call to xtime() can be omitted */
}
#else
#define Multiply(x, y)                                  \
    (  ((y & 1) * x) ^                                  \
       ((y>>1 & 1) * xtime(x)) ^                        \
       ((y>>2 & 1) * xtime(xtime(x))) ^                 \
       ((y>>3 & 1) * xtime(xtime(xtime(x)))) ^          \
       ((y>>4 & 1) * xtime(xtime(xtime(xtime(x))))))    \

#endif
// Cipher is the main function that encrypts the PlainText.
static void Cipher(state_t* st, const U8* rk)
{
    // Add the First round key to the state before starting
    AddRoundKey(0, st, rk);
    // There will be Nr rounds.
    // The first Nr-1 rounds are identical.
    // These Nr rounds are executed in the loop below.
    // Last one without MixColumns()
    for (U8 n = 1; ; ++n) {
        SubBytes(st);
        ShiftRows(st);
        if (n == Nr) break;
        
        MixColumns(st);
        AddRoundKey(n, st, rk);
    }
    // Add round key to last round
    AddRoundKey(Nr, st, rk);
}
/* Symmetrical operation: same function for encrypting as for decrypting. Note any IV/nonce should never be reused with the same key */
void xcrypt_buffer(struct AES_ctx* ctx, U8* buf, size_t length)
{
    U8  st[AES_NBLOCK];
    int bi = AES_NBLOCK;
    for (size_t i = 0; i < length; ++i, ++bi) {
        if (bi == AES_NBLOCK) { /* we need to regen xor compliment in buffer */
            memcpy(st, ctx->IV, AES_NBLOCK);
            Cipher((state_t*)st, ctx->RK);

            /* increment IV and handle overflow */
            for (bi = (AES_NBLOCK - 1); bi >= 0; --bi) {
                if (ctx->IV[bi] != 255) {
                    ctx->IV[bi]++;
                    break;
                }
                /* overflow */
                ctx->IV[bi] = 0;
            }
            bi = 0;
        }
        buf[i] ^= st[bi];
    }
}
// Enable ECB, CTR and CBC mode. Note this can be done before including aes.h or at compile-time.
// E.g. with GCC by using the -D flag: gcc -c aes.c -DCBC=0 -DCTR=1 -DECB=1
static int test_ctr()
{
#if AES_BITS==256
    U8 key[32] = { 0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe, 0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
                   0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7, 0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4 };
    U8 in[64]  = { 0x60, 0x1e, 0xc3, 0x13, 0x77, 0x57, 0x89, 0xa5, 0xb7, 0xa7, 0xf5, 0x04, 0xbb, 0xf3, 0xd2, 0x28, 
                   0xf4, 0x43, 0xe3, 0xca, 0x4d, 0x62, 0xb5, 0x9a, 0xca, 0x84, 0xe9, 0x90, 0xca, 0xca, 0xf5, 0xc5, 
                   0x2b, 0x09, 0x30, 0xda, 0xa2, 0x3d, 0xe9, 0x4c, 0xe8, 0x70, 0x17, 0xba, 0x2d, 0x84, 0x98, 0x8d, 
                   0xdf, 0xc9, 0xc5, 0x8d, 0xb6, 0x7a, 0xad, 0xa6, 0x13, 0xc2, 0xdd, 0x08, 0x45, 0x79, 0x41, 0xa6 };
#elif AES_BITS==192
    U8 key[24] = { 0x8e, 0x73, 0xb0, 0xf7, 0xda, 0x0e, 0x64, 0x52, 0xc8, 0x10, 0xf3, 0x2b, 0x80, 0x90, 0x79, 0xe5, 
                   0x62, 0xf8, 0xea, 0xd2, 0x52, 0x2c, 0x6b, 0x7b };
    U8 in[64]  = { 0x1a, 0xbc, 0x93, 0x24, 0x17, 0x52, 0x1c, 0xa2, 0x4f, 0x2b, 0x04, 0x59, 0xfe, 0x7e, 0x6e, 0x0b, 
                   0x09, 0x03, 0x39, 0xec, 0x0a, 0xa6, 0xfa, 0xef, 0xd5, 0xcc, 0xc2, 0xc6, 0xf4, 0xce, 0x8e, 0x94, 
                   0x1e, 0x36, 0xb2, 0x6b, 0xd1, 0xeb, 0xc6, 0x70, 0xd1, 0xbd, 0x1d, 0x66, 0x56, 0x20, 0xab, 0xf7, 
                   0x4f, 0x78, 0xa7, 0xf6, 0xd2, 0x98, 0x09, 0x58, 0x5a, 0x97, 0xda, 0xec, 0x58, 0xc6, 0xb0, 0x50 };
#elif AES_BITS==128
    U8 key[16] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };
    U8 in[64]  = { 0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26, 0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
                   0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff, 0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff,
                   0x5a, 0xe4, 0xdf, 0x3e, 0xdb, 0xd5, 0xd3, 0x5e, 0x5b, 0x4f, 0x09, 0x02, 0x0d, 0xb0, 0x3e, 0xab,
                   0x1e, 0x03, 0x1d, 0xda, 0x2f, 0xbe, 0x03, 0xd1, 0x79, 0x21, 0x70, 0xa0, 0xf3, 0x00, 0x9c, 0xee };
#endif
    U8 iv[16]  = { 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff };
    U8 out[64] = { 0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
                   0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
                   0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
                   0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17, 0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10 };
    struct AES_ctx ctx;
    
    KeyExpansion(ctx.RK, key);        // init ctx
    memcpy(ctx.IV, iv, AES_NBLOCK);   // init iv
    
    xcrypt_buffer(&ctx, in, 64);      // both encrypt/decrypt use the same function
  
    return memcmp((char*)out, (char*)in, 64);
}

#include <stdio.h>
int main(void)
{
    printf("encrypt=%s\n", test_ctr() ? "Failed" : "OK");
    printf("decrypt=%s\n", test_ctr() ? "Failed" : "OK");
}


