#ifndef __TWOFISH_H
#define __TWOFISH_H

typedef struct
{
        u_int32_t l_key[40];
        u_int32_t s_key[4];
        u_int32_t mk_tab[4*256];
        u_int32_t k_len;
} twofish_ctx;

void twofish_set_key(twofish_ctx *ctx, const u_int8_t in_key[], int key_len);
void twofish_encrypt(twofish_ctx *ctx, const u_int8_t in_blk[], u_int8_t out_blk[]);
void twofish_decrypt(twofish_ctx *ctx, const u_int8_t in_blk[], u_int8_t out_blk[]);

#endif /* __TWOFISH_H */
