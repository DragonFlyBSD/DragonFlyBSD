/*
 *
 */

struct sha256_ctx
{
	uint32_t H[8];

	uint32_t total[2];
	uint32_t buflen;
	char buffer[128]; /* NB: always correctly aligned for uint32_t.  */
};

struct sha512_ctx
{
	uint64_t H[8];

	uint64_t total[2];
	uint64_t buflen;
	char buffer[256]; /* NB: always correctly aligned for uint64_t.  */
};

void __crypt__sha256_process_block (const void *buffer, size_t len,
			struct sha256_ctx *ctx);
void __crypt__sha256_init_ctx (struct sha256_ctx *ctx);
void *__crypt__sha256_finish_ctx (struct sha256_ctx *ctx, void *resbuf);
void __crypt__sha256_process_bytes (const void *buffer, size_t len,
			struct sha256_ctx *ctx);

void __crypt__sha512_process_block (const void *buffer, size_t len,
			struct sha512_ctx *ctx);
void __crypt__sha512_init_ctx (struct sha512_ctx *ctx);
void *__crypt__sha512_finish_ctx (struct sha512_ctx *ctx, void *resbuf);
void __crypt__sha512_process_bytes (const void *buffer, size_t len, struct sha512_ctx *ctx);
void __crypt__sha512_process_block (const void *buffer, size_t len, struct sha512_ctx *ctx);
void __crypt__sha512_init_ctx (struct sha512_ctx *ctx);
