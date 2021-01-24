// lib

#ifndef ARMCOMMLIB_H
#define ARMCOMMLIB_H

#include "../AArch64cryptolib/AArch64cryptolib.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#define operation_result_t  armv8_operation_result_t

#define cipher_mode_t       armv8_cipher_mode_t
#define operation_result_t  armv8_operation_result_t
#define doubleword_t        armv8_doubleword_t
#define quadword_t          armv8_quadword_t
#define cipher_state_t      armv8_cipher_state_t

#define aes_gcm_set_counter             armv8_aes_gcm_set_counter
#define encrypt_from_state              armv8_enc_aes_gcm_from_state
#define decrypt_from_state              armv8_dec_aes_gcm_from_state

uint8_t *encrypt(char *message, int mode);
uint8_t *decrypt(uint8_t *message, int mode);

void en_aes_cbc_sha1(uint8_t *enc_val, uint8_t **cipher_output);
void de_aes_cbc_sha1(uint8_t *dec_val, uint8_t **plain_output);

void en_aes_cbc_sha256(uint8_t *enc_val, uint8_t **cipher_output);
void de_aes_cbc_sha256(uint8_t *dec_val, uint8_t **plain_output);

void en_aes_gcm_state(uint8_t *enc_val, uint8_t **cipher_output);
void de_aes_gcm_state(uint8_t *dec_val, uint8_t **plain_output);

#endif
