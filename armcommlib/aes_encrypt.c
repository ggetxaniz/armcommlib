#include "../AArch64cryptolib/AArch64cryptolib.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define operation_result_t  armv8_operation_result_t
#define quadword_t          armv8_quadword_t
#define cipher_constants_t  armv8_cipher_constants_t
#define cipher_state_t      armv8_cipher_state_t

#define aes_gcm_set_counter             armv8_aes_gcm_set_counter
#define encrypt_from_state              armv8_enc_aes_gcm_from_state
#define decrypt_from_state              armv8_dec_aes_gcm_from_state


void en_aes_cbc_sha1(uint8_t *enc_val, uint8_t **cipher_output, int size, uint8_t *key, uint8_t **hash)
{
    uint64_t block_byte_length = 16;
    uint8_t iv[16] = {0};
    uint8_t key_expanded[256] = {0};

    armv8_cipher_digest_t arg;


    arg.digest.hmac.key = key;
    arg.digest.hmac.i_key_pad = key;
    arg.digest.hmac.o_key_pad = key;

    armv8_expandkeys_enc_aes_cbc_128(key_expanded, key);

    arg.cipher.key = key_expanded;
    arg.cipher.iv = iv;

    //// Get reference input file name

    operation_result_t encrypt_result = armv8_enc_aes_cbc_sha1_128(
                enc_val, *cipher_output, block_byte_length,
                enc_val, *hash, block_byte_length,
                &arg);
}


void en_aes_cbc_sha256(uint8_t *enc_val, uint8_t **cipher_output, int size, uint8_t *key, uint8_t **hash)
{
    uint64_t block_byte_length = 16;
    uint8_t iv[16] = {0};
    uint8_t key_expanded[256] = {0};

    armv8_cipher_digest_t arg;


    arg.digest.hmac.key = key;
    arg.digest.hmac.i_key_pad = key;
    arg.digest.hmac.o_key_pad = key;

    armv8_expandkeys_enc_aes_cbc_128(key_expanded, key);

    arg.cipher.key = key_expanded;
    arg.cipher.iv = iv;

    //// Get reference input file name

    operation_result_t encrypt_result = armv8_enc_aes_cbc_sha256_128(
                enc_val, *cipher_output, block_byte_length,
                enc_val, *hash, block_byte_length,
                &arg);
}

void en_aes_gcm_state(uint8_t *enc_val, uint8_t **cipher_output, int size, uint8_t *key, uint8_t **hash)
{

    cipher_constants_t cc = { .mode = 0 };
    cipher_state_t cs = { .counter = { .d = {0,0} } };
    cs.constants = &cc;

    uint8_t aad[16] = {0};
    uint64_t aad_length = 16;
    uint64_t plaintext_length = size;

    quadword_t temp_counter = cs.counter;
    uint8_t tag[16] = {0};

    cs.current_tag.d[0] = 0;
    cs.current_tag.d[1] = 0;

    operation_result_t encrypt_result = encrypt_from_state(
            &cs,
            aad, aad_length,
            enc_val, plaintext_length,     //Inputs
            *cipher_output,
            tag); //Outputs
}

