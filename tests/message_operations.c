#include <stdio.h>

#include "../armcommlib/armcommlib.h"

int main()
{
      char message[] = "El objetivo de este trabajo es el desarrollo de una libreria que permita la utilizacion de las extensiones criptograficas proporcionadas por ARM de manera sencilla. En particular, las funciones que implementan el estandar de cifrado AES y las funciones de resumen SHA-1 y SHA-2. La libreria permite cifrar/descifrar cualquier flujo de informacion como pueden ser mensajes enviados a traves de una canal de comunicacion, o incluso, el cifrado/descifrado de ficheros de una manera eficiente gracias al hardware criptografico que implementa la arquitectura ARMv8. ARM ya proporciona una libreria de bajo nivel (AArch64cryptolib) para utilizar las funciones hardware pero su uso es complicado y solo permite el uso de dichas funciones a muy bajo nivel.";
      uint8_t *enc_result, *dec_result;

      // Key must be in hexadecimal

      uint8_t key[16] = {0x10, 0xa5, 0x88, 0x69, 0xd7, 0x4b, 0xe5, 0xa3, 0x74, 0xcf, 0x86, 0x7c, 0xfb, 0x47, 0x38, 0x59};
      int i, size;
      uint8_t *hash;

      size = strlen(message);

      enc_result = (uint8_t *)malloc(size*sizeof(uint8_t));
      dec_result = (uint8_t *)malloc(size*sizeof(uint8_t));
      hash = (uint8_t *)malloc(16*sizeof(uint8_t));

      printf("\nText size: %u",size);

      printf("\nPlaintext: %s",message);
      printf("\n");

      // Encrypt message

      // Mode is 0 = aes_cbc_sha1, 1 = aes_cbc_sha256, 2 = aes_gcm

      encrypt(message,&enc_result,0,size,key,&hash);

      printf("\nEncryption: ");
      for( i=0; i<size; i++) {
          printf("%02x", enc_result[i]);
      }
      printf("\n");

      // Print hash value

      printf("\n Hash:");
      for( i=0; i<size; i++) {
          printf("%02x", hash[i]);
      }
      printf("\n");

      // Decrypt message

      // Mode is 0 = aes_cbc_sha1, 1 = aes_cbc_sha256, 2 = aes_gcm

      decrypt(enc_result,&dec_result,0,size,key,&hash);

      printf("\nDecryption: ");
      for( i=0; i<size; i++) {
          printf("%c", dec_result[i]);
      }
      printf("\n");

      // Print hash value

      printf("\n Hash:");
      for( i=0; i<size; i++) {
          printf("%02x", hash[i]);
      }
      printf("\n");

      return 0;
}
