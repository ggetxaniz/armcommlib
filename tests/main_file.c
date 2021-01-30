#include <stdio.h>

#include "../armcommlib/armcommlib.h"

int main(int argc, char *argv[])
{

      uint8_t *enc_result, *dec_result, *plain_file;
      uint8_t key[16] = {0x10, 0xa5, 0x88, 0x69, 0xd7, 0x4b, 0xe5, 0xa3, 0x74, 0xcf, 0x86, 0x7c, 0xfb, 0x47, 0x38, 0x59};
      int i,size;
      uint8_t *hash;

      plain_file = (uint8_t *)malloc(20*sizeof(uint8_t));
      hash = (uint8_t *)malloc(16*sizeof(uint8_t));

      FILE *file = fopen(argv[1], "r");

      if (file == NULL){
    	  exit(1);
      }else{
    	  fseek (file, 0, SEEK_END);
          size = ftell (file);
      }

      plain_file = (uint8_t *)argv[1];

      printf("Size of file: %u\n",size);

      printf("File: %s\n",plain_file);

      enc_result = (uint8_t *)malloc(size*sizeof(uint8_t));
      dec_result = (uint8_t *)malloc(size*sizeof(uint8_t));

      encrypt_file(plain_file,&enc_result,0,key,&hash);

      //decrypt_file(plain_file,&dec_result,0,key,hash);


      free(hash);
      free(enc_result);
      free(dec_result);
      free(plain_file);

      return 0;
}
