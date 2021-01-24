#include <stdio.h>

#include "../armcommlib/armcommlib.h"

int main()
{
      char *message;
      uint8_t *enc_result, *dec_result;

      message = malloc(128*sizeof(char));

      message = "Kaixo, zer moduz?";

      enc_result = encrypt(message,2);

      dec_result = decrypt(enc_result,2);

      return 0;
}
