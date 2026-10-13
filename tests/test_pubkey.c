/**
 * Host tests only: the public key the bootloader code verifies against.
 *
 * test_fw.c makes a key pair when it starts and copies the public half in here,
 * so no private key is kept anywhere. The product key is src/services/fw/fw_pubkey.c.
 * Written without fw_sign.h on purpose: the declaration there is const.
 */
#include <stdint.h>

uint8_t fw_sign_pubkey[64];
