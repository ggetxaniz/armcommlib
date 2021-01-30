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
	char message[1024] , server_reply[1024];

        uint8_t *enc_message, *dec_message, *hash;
        uint8_t key[16] = {0x10, 0xa5, 0x88, 0x69, 0xd7, 0x4b, 0xe5, 0xa3, 0x74, 0xcf, 0x86, 0x7c, 0xfb, 0x47, 0x38, 0x59};
        int size, i;

        hash = (uint8_t *)malloc(16*sizeof(uint8_t));

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

	//keep communicating with server
	while(1)
	{
		printf("Enter message: ");
		gets(message);

		size = strlen(message);

                printf("\nMessage size: %u\n",size);

                enc_message = (uint8_t *)malloc(size*sizeof(uint8_t));
                dec_message = (uint8_t *)malloc(size*sizeof(uint8_t));

		if (strncmp(message, "exit", 4) == 0) break;

                encrypt(message,&enc_message,0,size,key,&hash);

                /*
                size = strlen(enc_message);

                printf("ENC %u: ",size);

                for( i=0; i<size; i++) {
                    printf("%02x", enc_message[i]);
                }
                printf("\n");
                */

		//Send message
		if( send(sock , enc_message , size , 0) < 0)
		{
			puts("Send failed");
			return 1;
		}

		//Receive a reply from the server
		if( (read_size = recv(sock , server_reply , 1024 , 0)) < 0)
		{
			puts("recv failed");
			break;
		}


                decrypt(server_reply,&dec_message,0,read_size,key,&hash);
		puts("Server reply: ");

                for( i=0; i<read_size; i++) {
                    printf("%c", dec_message[i]);
                }
                printf("\n");

	}

	close(sock);
	return 0;
}
