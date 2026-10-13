/**
 ******************************************************************************
 * @brief:  Image signature: ECDSA P-256 over SHA-256 of the header's first
 *          48 bytes (magic .. board, sig_type) followed by the binary. The
 *          signature r||s sits in the header at offset 48; hdr_crc32 is
 *          computed after signing, so it covers the signature too.
 *
 *  Built with FW_IMAGE_SIGN=1 (CMake -DAK_IMAGE_SIGN=ON, PlatformIO
 *  image_sign = 1) the bootloader and the app refuse every image that is not
 *  signed with the private key matching fw_sign_pubkey (fw_pubkey.c, made by
 *  tools/ak_sign.py pubkey-c). Signing: tools/mkimage.py ... --sign-key.
 *  Verification: third_party/p256-m, about 3 KB of code and 750 B of stack;
 *  the bootloader of this variant is 16K (PORT_IMAGE_SIGN).
 ******************************************************************************
**/

#ifndef __FW_SIGN_H__
#define __FW_SIGN_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define FW_SIGN_NONE			(0)
#define FW_SIGN_ECDSA_P256		(1)		/* ECDSA P-256 / SHA-256, raw r||s */
#define FW_SIGN_LEN				(64)
#define FW_SIGN_HDR_LEN			(48)	/* header bytes covered by the hash */

/* uncompressed public key: X || Y, 32 bytes each, big-endian */
extern const uint8_t fw_sign_pubkey[64];

#if defined(AK_SIM)
/* Host tests: turn the signature requirement on or off (firmware: fixed). */
extern void fw_image_sign_require(uint8_t on);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __FW_SIGN_H__ */
