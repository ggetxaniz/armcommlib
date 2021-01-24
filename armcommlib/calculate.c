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

uint8_t *encrypt(char *message, int mode){

    void (*aes_encrypt)(uint8_t*, uint8_t**);

// Mode election

/*    if (mode != 0 || mode != 1 || mode != 2){
        printf("\nEncrypt/decrypt mode must be 0, 1, or 2.\n 0 = aes_cbc_sha1\n 1 = aes_cbc_sha256\n 2 = aes_gcm\n");
        return;
    }
*/
    switch (mode) {
        case 0:
            printf("\nYou seleceted aes_cbc_sha1 encrypt mode\n");
            aes_encrypt = &en_aes_cbc_sha1;
            break;
        case 1:
            printf("\nYou seleceted aes_cbc_sha256 encrypt mode\n");
            aes_encrypt = &en_aes_cbc_sha256;
            break;
        case 2:
            printf("\nYou seleceted aes_gcm encrypt mode\n");
            aes_encrypt = &en_aes_gcm_state;
            break;
    }

// Parameters

    int size;
    uint8_t i = 0;
    uint8_t *text;
    uint8_t *enc_result;
    uint64_t block;

// Block quantityy

    block = 1;
    size = strlen(message);
    if (size > 16){
        if (size%16 != 0){
            block = ((size/16)+1);
        }else{
            block = size/16;
        }
    }

    printf("\nMessage size: %u",size);
    printf("\nBlock quantity: %lu",block);
    printf("\nPlaintext: %s\n",message);

    text = (uint8_t *)malloc((block*16)*sizeof(uint8_t));
    enc_result = (uint8_t *)malloc((block*16)*sizeof(uint8_t));

    strncpy(text, message, strlen(message));

// Calls administration

    uint8_t *aux;

    while(i<block){
        aux=&enc_result[i*16];
        aes_encrypt(&text[i*16], &aux);
        i++;
    }

// Print encryption

    printf("\nEncryption: ");
    for( i=0; i<size; i++) {
        printf("%02x", enc_result[i]);
    }
    printf("\n");

    return enc_result;
}

uint8_t *decrypt(uint8_t *enc_message, int mode){

    void (*aes_decrypt)(uint8_t*, uint8_t**);

// Mode election

/*    if (mode != 0 || mode != 1 || mode != 2){
        printf("\nEncrypt/decrypt mode must be 0, 1, or 2.\n 0 = aes_cbc_sha1\n 1 = aes_cbc_sha256\n 2 = aes_gcm\n");
        return;
    }
*/
    switch (mode) {
        case 0:
            printf("\nYou seleceted aes_cbc_sha1 decrypt mode\n");
            aes_decrypt = &de_aes_cbc_sha1;
            break;
        case 1:
            printf("\nYou seleceted aes_cbc_sha256 decrypt mode\n");
            aes_decrypt = &de_aes_cbc_sha256;
            break;
        case 2:
            printf("\nYou seleceted aes_gcm decrypt mode\n");
            aes_decrypt = &de_aes_gcm_state;
            break;
    }

// Parameters

    int size;
    uint8_t i = 0;
    uint8_t *enc_text;
    uint8_t *dec_result;
    uint64_t block;

// Block quantityy

    block = 1;
    size = strlen(enc_message);
    if (size > 16){
        if (size%16 != 0){
            block = ((size/16)+1);
        }else{
            block = size/16;
        }
    }

    printf("\nMessage size: %u",size);
    printf("\nBlock quantity: %lu",block);
    printf("\nEncrypt message: %02x\n",enc_message);

    enc_text = (uint8_t *)malloc((block*16)*sizeof(uint8_t));
    dec_result = (uint8_t *)malloc(size*sizeof(uint8_t));

    strncpy(enc_text, enc_message, strlen(enc_message));

// Calls administration

    uint8_t *aux;

    while(i<block){
        aux=&dec_result[i*16];
        aes_decrypt(&enc_text[i*16], &aux);
        i++;
    }

// Print decryption function
    printf("\nDecryption: ");
    for( i=0; i<size; i++) {
        printf("%c", dec_result[i]);
    }
    printf("\n");

    return dec_result;
}

