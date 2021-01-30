#include <stdio.h>

#include "../armcommlib/armcommlib.h"

int main()
{
      char message[] = "ahora viene cuando la mata, si supera los 256 petara? lo veremos en el siguiente capitulo ...Kaixo, zer moduz? hemen probak  egiten gaude ea funtzionatzen duen lan haue... gero eta luzeagoa den testu batekin probatu behar dugu, horretarako mezua askoz luzeagoa izan behar da eta hori gaude egiten ea horrela funtzionatzen duen baina hau";
      uint8_t *enc_result, *dec_result;
      uint8_t key[16] = {0x10, 0xa5, 0x88, 0x69, 0xd7, 0x4b, 0xe5, 0xa3, 0x74, 0xcf, 0x86, 0x7c, 0xfb, 0x47, 0x38, 0x59};
      int i, size;
      uint8_t *hash;

      size = strlen(message);

      printf("\nText size: %u",size);

      printf("\nPlaintext: %s",message);
      printf("\n");

      enc_result = (uint8_t *)malloc(size*sizeof(uint8_t));
      dec_result = (uint8_t *)malloc(size*sizeof(uint8_t));
      hash = (uint8_t *)malloc(16*sizeof(uint8_t));

      encrypt(message,&enc_result,0,size,key,&hash);

      printf("\nEncryption: ");
      for( i=0; i<size; i++) {
          printf("%02x", enc_result[i]);
      }
      printf("\n");
      printf("\n Hash:");
      for( i=0; i<size; i++) {
          printf("%02x", hash[i]);
      }
      printf("\n");

      decrypt(enc_result,&dec_result,0,size,key,&hash);


      printf("\nDecryption: ");
      for( i=0; i<size; i++) {
          printf("%c", dec_result[i]);
      }
      printf("\n");
      printf("\n Hash:");
      for( i=0; i<size; i++) {
          printf("%02x", hash[i]);
      }
      printf("\n");

      return 0;
}
