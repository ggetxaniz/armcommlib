#ifndef aes_decrypt_h__
#define aes_decrypt_h__

#include "../AArch64cryptolib/AArch64cryptolib.h"

void de_aes_cbc_sha1(uint8_t *dec_val, uint8_t **plaint_output);
void de_aes_cbc_sha256(uint8_t *dec_val, uint8_t **plaint_output);
void de_aes_gcm_state(uint8_t *dec_val, uint8_t **plaint_output);

#endif   // aes_decrypt_h__
