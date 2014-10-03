
// Copyright in this code is held by Dr B. R. Gladman but free direct or
// derivative use is permitted subject to acknowledgement of its origin.
// Dr B. R. Gladman                               .   25th January 2000.

typedef struct
{
	uint32_t l_key[140];
} serpent_ctx;

void serpent_set_key(serpent_ctx *ctx, const u_int8_t in_key[], int key_len);
void serpent_encrypt(serpent_ctx *ctx, const u_int8_t in_blk[],
		     u_int8_t out_blk[]);
void serpent_decrypt(serpent_ctx *ctx, const u_int8_t in_blk[],
		     u_int8_t out_blk[]);
