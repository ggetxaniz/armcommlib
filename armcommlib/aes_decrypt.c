#include "../AArch64cryptolib/AArch64cryptolib.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define operation_result_t  armv8_operation_result_t

#define cipher_mode_t       armv8_cipher_mode_t
#define operation_result_t  armv8_operation_result_t
#define doubleword_t        armv8_doubleword_t
#define quadword_t          armv8_quadword_t
#define cipher_constants_t  armv8_cipher_constants_t
#define cipher_state_t      armv8_cipher_state_t

#define aes_gcm_set_counter             armv8_aes_gcm_set_counter
#define encrypt_full                    armv8_enc_aes_gcm_full
#define encrypt_from_state              armv8_enc_aes_gcm_from_state
#define encrypt_from_constants_IPsec    armv8_enc_aes_gcm_from_constants_IPsec
#define decrypt_full                    armv8_dec_aes_gcm_full
#define decrypt_from_state              armv8_dec_aes_gcm_from_state
#define decrypt_from_constants_IPsec    armv8_dec_aes_gcm_from_constants_IPsec

#include "aes_decrypt.h"

void de_aes_cbc_sha1(uint8_t *dec_val, uint8_t **plain_output)
{
    uint64_t block_byte_length = 16;
    uint8_t *auth;
    uint8_t key[16] = {0};
    uint8_t iv[16] = {0};
    uint8_t key_expanded[256] = {0};

    armv8_cipher_digest_t arg;

    auth = (uint8_t *)malloc(block_byte_length);

    arg.digest.hmac.key = key;
    arg.digest.hmac.i_key_pad = key;
    arg.digest.hmac.o_key_pad = key;

    armv8_expandkeys_dec_aes_cbc_128(key_expanded, key);

    arg.cipher.key = key_expanded;
    arg.cipher.iv = iv;

    //// Get reference input file name

    operation_result_t decrypt_result = armv8_dec_aes_cbc_sha1_128(
                dec_val, *plain_output, block_byte_length,
                *plain_output, auth, block_byte_length,
                &arg);
}

void de_aes_cbc_sha256(uint8_t *dec_val, uint8_t **plain_output)
{
    uint64_t block_byte_length = 128;
    uint8_t *auth;
    uint8_t key[16] = {0};
    uint8_t iv[16] = {0};
    uint8_t key_expanded[256] = {0};

    armv8_cipher_digest_t arg;

    auth = (uint8_t *)malloc(block_byte_length);

    arg.digest.hmac.key = key;
    arg.digest.hmac.i_key_pad = key;
    arg.digest.hmac.o_key_pad = key;

    armv8_expandkeys_dec_aes_cbc_128(key_expanded, key);

    arg.cipher.key = key_expanded;
    arg.cipher.iv = iv;

    //// Get reference input file name

    operation_result_t decrypt_result = armv8_dec_aes_cbc_sha256_128(
                dec_val, *plain_output, block_byte_length,
                *plain_output, auth, block_byte_length,
                &arg);
}

void de_aes_gcm_state(uint8_t *dec_val, uint8_t **plain_output)
{

    cipher_constants_t cc = { .mode = 0 };
    cipher_state_t cs = { .counter = { .d = {0,0} } };
    cs.constants = &cc;

//    uint8_t reference_tag[16] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };

    uint8_t key_length = 16;
    uint8_t key[16] = {0};

    uint8_t aad[16] = {0};
    uint64_t aad_length = 16;
    uint64_t plaintext_length = 128;

    quadword_t temp_counter = cs.counter;
    uint8_t tag[16] = {0};

    cs.current_tag.d[0] = 0;
    cs.current_tag.d[1] = 0;

    // Encryption done in place, need to copy reference to output
//    memcpy(*plain_output, dec_val, plaintext_byte_length);

    // IPsec encrypt requires aad to be zero padded
//    uint8_t zero_padded_aad[16] = { 0 };
//    memcpy(zero_padded_aad, aad, aad_length>>3);

//    tag = (uint8_t *)malloc(cs.constants->tag_byte_length);

    operation_result_t decrypt_result = decrypt_from_state(
            &cs,
            aad, aad_length,
            dec_val, plaintext_length,     //Inputs
            tag,
            *plain_output); //Outputs
}

