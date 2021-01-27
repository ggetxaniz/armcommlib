#include <stdio.h>

#include "../armcommlib/armcommlib.h"

int main()
{
      char message[] = "ahora viene cuando la mata, si supera los 256 petara? lo veremos en el siguiente capitulo ...Kaixo, zer moduz? hemen probak  egiten gaude ea funtzionatzen duen lan haue... gero eta luzeagoa den testu batekin probatu behar dugu, horretarako mezua askoz luzeagoa izan behar da eta hori gaude egiten ea horrela funtzionatzen duen baina hau";
      uint8_t *enc_result, *dec_result;
      int i, size;

      size = strlen(message);

      printf("\nSize: %u",size);

      printf("\nPlaintext: %s",message);
      printf("\n");
      enc_result = encrypt(message,1);

      printf("\nEncryption: ");
      for( i=0; i<size; i++) {
          printf("%02x", enc_result[i]);
      }
      printf("\n");


      dec_result = decrypt(enc_result,1);

      printf("\nDecryption: ");
      for( i=0; i<size; i++) {
          printf("%c", dec_result[i]);
      }
      printf("\n");



      return 0;
}
