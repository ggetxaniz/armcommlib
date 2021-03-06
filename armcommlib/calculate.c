#include "../AArch64cryptolib/AArch64cryptolib.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "armcommlib.h"

#define CHECK_BIT(var,pos) ((var) & (1<<(pos)))

static inline uint64_t get_info(void)
{
	uint64_t info;
	asm("MRS %[result], ID_AA64ISAR0_EL1": [result] "=r" (info));
	return info;
}

void check_crypto(void)
{
    uint64_t reg_info = get_info();

    if (CHECK_BIT(reg_info,2)){
        printf("This system does not support AES and SHA functios.");
	exit(0);
    }
    if (CHECK_BIT(reg_info,3)){
        printf("This system does not support AES and SHA functios.");
	exit(0);
    }
    if (CHECK_BIT(reg_info,4)){
        printf("This system does not support AES and SHA functios.");
	exit(0);
    }
}

uint8_t encrypt(uint8_t *message, uint8_t** enc_result, int mode, int size, uint8_t *key, uint8_t **hash){

    void (*aes_encrypt)(uint8_t*, uint8_t**, int, uint8_t*, uint8_t**);

    check_crypto();

// Mode election

    if (size == 0){
	exit(0);
    }
    if (mode < 0 || mode > 2){
        exit(0);
    }else{
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
    }

// Parameters

    uint64_t i = 0;
    uint8_t *text;
    uint8_t *aux_result;
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
    aux_result = (uint8_t *)malloc((block*16)*sizeof(uint8_t));

    memcpy(text, message, size);

// Calls administration

    uint8_t *aux;

    while(i<block){
        aux=&aux_result[i*16];
        aes_encrypt(&text[i*16], &aux, size, key, hash);
        i++;
    }
    memcpy(*enc_result, aux_result, size);
    free(aux_result);
    free(text);
    return(1);
}

uint8_t decrypt(uint8_t *enc_message, uint8_t **dec_result, int mode, int size, uint8_t *key, uint8_t **hash){

    void (*aes_decrypt)(uint8_t*, uint8_t**, int, uint8_t*, uint8_t**);

    check_crypto();

// Mode election

    if (size <= 0){
        return(0);
    }
    if (mode < 0 || mode > 2){
        return(0);
    }else{
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
    }

// Parameters

    uint64_t i = 0;
    uint8_t *enc_text;
    uint8_t *aux_result;
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
    aux_result = (uint8_t *)malloc((block*16)*sizeof(uint8_t));

    memcpy(enc_text, enc_message, size);

// Calls administration

    uint8_t *aux;

    while(i<block){
        aux=&aux_result[i*16];
        aes_decrypt(&enc_text[i*16], &aux, size, key, hash);
        i++;
    }
    memcpy(*dec_result, aux_result, size);
    free(aux_result);
    free(enc_text);
    return (1);
}

void encrypt_file(uint8_t *plain_file, int mode, uint8_t *key){

    void (*aes_encrypt)(uint8_t*, uint8_t**, int, uint8_t*, uint8_t**);

    check_crypto();

// Mode election
    if (mode < 0 || mode > 2){
        return;
    }else{
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
    }

// Parameters
    int size;
    uint64_t i = 0;
    uint8_t *text;
    uint8_t *aux_result;
    uint64_t block;
    uint8_t *buffer = 0;
    uint8_t *hash;

    hash = (uint8_t *)malloc(16*sizeof(uint8_t));

// Open file

    FILE *file = fopen(plain_file, "r+");

    if (file == NULL){
    	exit(1);
    }else{
    	fseek (file, 0, SEEK_END);
        size = ftell (file);
        fseek (file, 0, SEEK_SET);
        buffer = (uint8_t *)malloc (size*sizeof(uint8_t));

        if(buffer){
            fread(buffer, sizeof(uint8_t), size, file);
	    fseek (file, 0, SEEK_SET);
        }

	// Block quantity

        block = 1;
        if (size > 16){
            if (size%16 != 0){
        	block = ((size/16)+1);
            }else{
        	block = size/16;
            }
        }

        text = (uint8_t *)malloc((block*16)*sizeof(uint8_t));

        aux_result = (uint8_t *)malloc((block*16)*sizeof(uint8_t));

        memcpy(text, buffer, size);

        uint8_t *aux;

// Calls administration

        while(i<block){
            aux=&aux_result[i*16];
            aes_encrypt(&text[i*16], &aux, size, key, &hash);
            i++;
    	}

        fwrite(aux_result, sizeof(uint8_t),size, file);
        free(aux_result);
    	free(text);
    }
    fclose(file);
}


void decrypt_file(uint8_t *enc_file, int mode, uint8_t *key){

    void (*aes_decrypt)(uint8_t*, uint8_t**, int, uint8_t*, uint8_t**);

    check_crypto();

// Mode election

    if (mode < 0 || mode > 2){
        return;
    }else{
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
    }

// Parameters

    int size;
    uint64_t i = 0;
    uint8_t *enc_text;
    uint8_t *aux_result;
    uint64_t block;
    uint8_t *buffer = 0;
    uint8_t *hash;

    hash = (uint8_t *)malloc(16*sizeof(uint8_t));

// Open file

    FILE *file = fopen(enc_file, "r+");

    if (file == NULL){
        exit(1);
    }else{
        fseek (file, 0, SEEK_END);
        size = ftell (file);
        fseek (file, 0, SEEK_SET);
        buffer = (uint8_t *)malloc (size*sizeof(uint8_t));

        if(buffer){
            fread(buffer, sizeof(uint8_t), size, file);
            fseek (file, 0, SEEK_SET);
        }

// Block quantity

   	block = 1;
    	if (size > 16){
       	    if (size%16 != 0){
       	    	block = ((size/16)+1);
            }else{
            	block = size/16;
            }
    	}

        enc_text = (uint8_t *)malloc((block*16)*sizeof(uint8_t));

    	aux_result = (uint8_t *)malloc((block*16)*sizeof(uint8_t));

        memcpy(enc_text, buffer, size);

    	uint8_t *aux;

// Calls administration

    	while(i<block){
            aux=&aux_result[i*16];
            aes_decrypt(&enc_text[i*16], &aux, size, key, &hash);
            i++;
    	}

        fwrite(aux_result, sizeof(uint8_t),size, file);
    	free(aux_result);
    	free(enc_text);
    }
    fclose(file);
}
