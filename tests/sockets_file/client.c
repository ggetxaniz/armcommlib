// CLIENT using Sockets in C
#include <stdio.h>	//printf
#include <stdlib.h>
#include <string.h>	//strlen
#include <sys/socket.h>	//socket
#include <arpa/inet.h>	//inet_addr
#include <unistd.h>
#define PORT 8080

#include "../../armcommlib/armcommlib.h"

int main(int argc , char *argv[])
{
	int sock, read_size;
	struct sockaddr_in server;
	char file_name[20];

        uint8_t *hash, *buffer, *enc_result;
        uint8_t key[16] = {0x10, 0xa5, 0x88, 0x69, 0xd7, 0x4b, 0xe5, 0xa3, 0x74, 0xcf, 0x86, 0x7c, 0xfb, 0x47, 0x38, 0x59};
        int size, i;
        char size_aux[4];

        hash = (uint8_t *)malloc(16*sizeof(uint8_t));

        strncpy(file_name, argv[1], strlen(argv[1]));

	//Create socket
	sock = socket(AF_INET , SOCK_STREAM , 0);
	if (sock == -1)
	{
		printf("Could not create socket");
	}
	puts("Socket created");

	server.sin_addr.s_addr = inet_addr("127.0.0.1");
	server.sin_family = AF_INET;
	server.sin_port = htons( PORT );

	//Connect to remote server
	if (connect(sock , (struct sockaddr *)&server , sizeof(server)) < 0)
	{
		perror("connect failed. Error");
		return 1;
	}

	puts("Connected\n");

	//Communicate with server
	printf("Send file %s", file_name);

	FILE *file = fopen(file_name, "r");

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

	        enc_result = (uint8_t *)malloc(size*sizeof(uint8_t));

		encrypt(buffer,&enc_result,0,size,key,&hash);

                memcpy(size_aux, &size, sizeof(size));

                //Send message
                if( send(sock , (const char*)size_aux, 4, 0) < 0)
                {
                        puts("Send failed");
                        return 1;
                }


		//Send message
		if( send(sock , enc_result , size , 0) < 0)
		{
			puts("Send failed");
			return 1;
		}
	}

	close(sock);
	return 0;
}
