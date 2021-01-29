#include "../AArch64cryptolib/AArch64cryptolib.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "armcommlib.h"

uint8_t encrypt(uint8_t *message, uint8_t** enc_result, int mode, int size, uint8_t *key){

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
    uint8_t *enc_result2;
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

//printf("Block %lu %d\n", block, size);
    text = (uint8_t *)malloc((block*16)*sizeof(uint8_t));
    enc_result2 = (uint8_t *)malloc((block*16)*sizeof(uint8_t));

    memcpy(text, message, size);

// Calls administration

    uint8_t *aux;

    while(i<block){
        aux=&enc_result2[i*16];
        aes_encrypt(&text[i*16], &aux, size, key);
        i++;
    }
    memcpy(*enc_result, enc_result2, size);
    free(enc_result2);
    free(text);
    return(1);
}

uint8_t decrypt(uint8_t *enc_message, uint8_t **dec_result, int mode, int size, uint8_t *key){

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
    uint8_t *dec_result2;
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

    //  for( i=0; i<size; i++) {
//	                printf("%02x", enc_message[i]);
//			      }
            printf("\n");


//printf("Block %lu %lu %lu \n", block, block*16,size  );
    enc_text = (uint8_t *)malloc((block*16)*sizeof(uint8_t));
    dec_result2 = (uint8_t *)malloc((block*16)*sizeof(uint8_t));

    memcpy(enc_text, enc_message, size);
//for( i=0; i<size; i++) {
//	                printf("%02x", enc_text[i]);
//			      }
  //          printf("\n");


// Calls administration

    uint8_t *aux;

    while(i<block){
        aux=&dec_result2[i*16];
        aes_decrypt(&enc_text[i*16], &aux, size, key);
        i++;
    }
    memcpy(*dec_result, dec_result2, size);
    free(dec_result2);
    free(enc_text);
    return(1);
}

void encrypt_file(uint8_t *plain_file, uint8_t** enc_result, int mode, uint8_t *key){

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
    int size;
    uint64_t i = 0;
    uint8_t *text;
    uint8_t *enc_result2;
    uint64_t block;
    uint8_t *buffer = 0;

// Block quantityy

    FILE *file = fopen(plain_file, "w+");

    if (file == NULL){
    	exit(1);
    }else{
    	fseek (file, 0, SEEK_END);
        size = ftell (file);
        fseek (file, 0, SEEK_SET);
        buffer = (uint8_t *)malloc (size*sizeof(uint8_t));

        if(buffer){
        	fread(buffer, sizeof(uint8_t), size, file);
        }

        block = 1;
        if (size > 16){
        	if (size%16 != 0){
        		block = ((size/16)+1);
        	}else{
        		block = size/16;
        	}
        }

        enc_result2 = (uint8_t *)malloc((block*16)*sizeof(uint8_t));

        uint8_t *aux;

        while(i<block){
        	aux=&enc_result2[i*16];
        	aes_encrypt(&buffer[i*16], &aux, size, key);
        	fprintf(file, "%02x", aux[i*16]);
        	i++;
    	}
    	memcpy(*enc_result, enc_result2, size);
   	 	free(enc_result2);
    	free(text);
    }
}


void decrypt_file(uint8_t *enc_file, uint8_t **dec_result, int mode, uint8_t *key){

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

    int size;
    uint64_t i = 0;
    uint8_t *enc_text;
    uint8_t *dec_result2;
    uint64_t block;
    uint8_t *buffer = 0;

// Block quantityy

    FILE *file = fopen(enc_file, "w+");

    if (file == NULL){
        exit(1);
    }else{
        fseek (file, 0, SEEK_END);
        size = ftell (file);
        fseek (file, 0, SEEK_SET);
        buffer = (uint8_t *)malloc (size*sizeof(uint8_t));

        if(buffer){
            fread(buffer, sizeof(uint8_t), size, file);
        }

   	 	block = 1;
    	if (size > 16){
       		if (size%16 != 0){
       	    	block = ((size/16)+1);
        	}else{
            	block = size/16;
        	}
    	}

    	dec_result2 = (uint8_t *)malloc((block*16)*sizeof(uint8_t));

    	uint8_t *aux;

    	while(i<block){
        	aux=&dec_result2[i*16];
        	aes_decrypt(&enc_text[i*16], &aux, size, key);
        	fprintf(file, "%c", aux[i*16]);
        	i++;
    	}

    	memcpy(*dec_result, dec_result2, size);
    	free(dec_result2);
    	free(enc_text);
    }

}
