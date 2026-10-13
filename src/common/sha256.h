/**
 ******************************************************************************
 * @brief:  SHA-256 (FIPS 180-4), streaming, no allocation. Used to hash a
 *          firmware image for its ECDSA signature.
 ******************************************************************************
**/

#ifndef __SHA256_H__
#define __SHA256_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define SHA256_LEN		(32)

typedef struct {
	uint32_t h[8];
	uint8_t  buf[64];
	uint32_t buf_len;
	uint32_t total_lo;		/* bytes hashed so far, 64 bit */
	uint32_t total_hi;
} sha256_t;

extern void sha256_init(sha256_t* s);
extern void sha256_update(sha256_t* s, const void* data, uint32_t len);
extern void sha256_final(sha256_t* s, uint8_t out[SHA256_LEN]);

/* one call for a buffer in memory */
extern void sha256(const void* data, uint32_t len, uint8_t out[SHA256_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* __SHA256_H__ */
