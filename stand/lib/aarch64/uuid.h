#ifndef _UUID_H_
#define _UUID_H_

#include <sys/types.h>
#include <sys/uuid.h>

#define uuid_s_ok 0
#define uuid_s_bad_version 1
#define uuid_s_invalid_string_uuid 2
#define uuid_s_no_memory 3
#define uuid_s_not_found 4

__BEGIN_DECLS
int32_t uuid_compare(const uuid_t *, const uuid_t *, uint32_t *);
void uuid_create(uuid_t *, uint32_t *);
void uuid_create_nil(uuid_t *, uint32_t *);
int32_t uuid_equal(const uuid_t *, const uuid_t *, uint32_t *);
void uuid_from_string(const char *, uuid_t *, uint32_t *);
uint16_t uuid_hash(const uuid_t *, uint32_t *);
int32_t uuid_is_nil(const uuid_t *, uint32_t *);
void uuid_to_string(const uuid_t *, char **, uint32_t *);
void uuid_enc_le(void *, const uuid_t *);
void uuid_dec_le(const void *, uuid_t *);
void uuid_enc_be(void *, const uuid_t *);
void uuid_dec_be(const void *, uuid_t *);
void uuid_reset_lookup(void);
void uuid_addr_lookup(const uuid_t *, char **, uint32_t *);
void uuid_name_lookup(uuid_t *, const char *, uint32_t *);
__END_DECLS

#endif
