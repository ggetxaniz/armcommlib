#include <stdio.h>

#include "../armcommlib/armcommlib.h"

int main(int argc, char *argv[])
{

      // Key must be in hexadecimal

      uint8_t key[16] = {0x10, 0xa5, 0x88, 0x69, 0xd7, 0x4b, 0xe5, 0xa3, 0x74, 0xcf, 0x86, 0x7c, 0xfb, 0x47, 0x38, 0x59};
      int i,size;

      printf("File: %s\n",argv[1]);

      printf("Encrypting\n");

      // Encrypt file

      encrypt_file(argv[1],0,key);

      printf("Decrypting\n");

      // Decrypt file

      decrypt_file(argv[1],0,key);

      return 0;
}
