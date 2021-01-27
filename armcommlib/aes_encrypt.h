#ifndef aes_encrypt_h__
#define aes_encrypt_h__

#include "../AArch64cryptolib/AArch64cryptolib.h"

void en_aes_cbc_sha1(uint8_t *enc_val, uint8_t **cipher_output, int size, uint8_t *key);
void en_aes_cbc_sha256(uint8_t *enc_val, uint8_t **cipher_output, int size, uint8_t *key);
void en_aes_gcm_state(uint8_t *enc_val, uint8_t **cipher_output, int size, uint8_t *key);

#endif   // aes_encrypt_h__
