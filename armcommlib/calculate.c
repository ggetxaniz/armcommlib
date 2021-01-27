#include "../AArch64cryptolib/AArch64cryptolib.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "aes_encrypt.h"
#include "aes_decrypt.h"

#include "armcommlib.h"

uint8_t *encrypt(char *message, int mode, int size, uint8_t *key){

    void (*aes_encrypt)(uint8_t*, uint8_t**, int, uint8_t*);

// Mode election

    switch (mode) {
        case 0:
            aes_encrypt = &en_aes_cbc_sha1;
            break;
        case 1:
            aes_encrypt = &en_aes_cbc_sha256;
            break;
        case 2:
            aes_encrypt = &en_aes_gcm_state;
            break;
    }

// Parameters

    uint64_t i = 0;
    uint8_t *text;
    uint8_t *enc_result;
    uint64_t block;

// Block quantityy

    block = 1;
    if (size > 16){
        if (size%16 != 0){
            block = ((size/16)+1);
        }else{
            block = size/16;
        }
    }

    text = (uint8_t *)malloc((block*16)*sizeof(uint8_t));
    enc_result = (uint8_t *)malloc((block*16)*sizeof(uint8_t));

    strncpy(text, message, size);

// Calls administration

    uint8_t *aux;

    while(i<block){
        aux=&enc_result[i*16];
        aes_encrypt(&text[i*16], &aux, size, key);
        i++;
    }

    return enc_result;
}

uint8_t *decrypt(uint8_t *enc_message, int mode, int size, uint8_t *key){

    void (*aes_decrypt)(uint8_t*, uint8_t**, int, uint8_t*);

// Mode election

    switch (mode) {
        case 0:
            aes_decrypt = &de_aes_cbc_sha1;
            break;
        case 1:
            aes_decrypt = &de_aes_cbc_sha256;
            break;
        case 2:
            aes_decrypt = &de_aes_gcm_state;
            break;
    }

// Parameters

    uint64_t i = 0;
    uint8_t *enc_text;
    uint8_t *dec_result;
    uint64_t block;

// Block quantityy

    block = 1;
    if (size > 16){
        if (size%16 != 0){
            block = ((size/16)+1);
        }else{
            block = size/16;
        }
    }

    enc_text = (uint8_t *)malloc((block*16)*sizeof(uint8_t));
    dec_result = (uint8_t *)malloc((block*16)*sizeof(uint8_t));

    strncpy(enc_text, enc_message, size);

// Calls administration

    uint8_t *aux;

    while(i<block){
        aux=&dec_result[i*16];
        aes_decrypt(&enc_text[i*16], &aux, size, key);
        i++;
    }

    return dec_result;
}

