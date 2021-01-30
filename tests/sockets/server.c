// SERVER using Sockets in C
#include<stdio.h>
#include<stdlib.h>
#include<string.h>	//strlen
#include<sys/socket.h>
#include<arpa/inet.h>	//inet_addr
#include<unistd.h>	//write
#define PORT 8080

#include "../../armcommlib/armcommlib.h"

int main(int argc , char *argv[])
{
	int socket_desc , client_sock , c , read_size;
	struct sockaddr_in server , client;
	char client_message[1024], message[1024];
	char *client_ip;

        uint8_t *enc_message, *dec_message, *hash;
        uint8_t key[16] = {0x10, 0xa5, 0x88, 0x69, 0xd7, 0x4b, 0xe5, 0xa3, 0x74, 0xcf, 0x86, 0x7c, 0xfb, 0x47, 0x38, 0x59};
        int size, i;

	//Create socket

	socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1)
	{
		printf("Could not create socket");
	}
	puts("Socket created");

	//Prepare the sockaddr_in structure
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons( PORT );

	//Bind
	if( bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0)
	{
		//print the error message
		perror("bind failed. Error");
		return 1;
	}
	puts("bind done");

	//Listen
	listen(socket_desc , 3);

	//Accept and incoming connection
	puts("Waiting for incoming connections...");
	c = sizeof(struct sockaddr_in);

	//accept connection from an incoming client
	client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c);
	if (client_sock < 0)
	{
		perror("accept failed");
		return 1;
	}
	puts("Connection accepted");

	client_ip = inet_ntoa(client.sin_addr);

	//Receive a message from client
	while( read_size > 0 || client_message != "exit" )
	{
		read_size = recv(client_sock , client_message , 1024 , 0);

                printf("\nMessage size: %u\n",read_size);

                enc_message = (uint8_t *)malloc(size*sizeof(uint8_t));
                dec_message = (uint8_t *)malloc(size*sizeof(uint8_t));


                decrypt(client_message,&dec_message,0,read_size,key,hash);

                printf("%s :\n",client_ip);

                for( i=0; i<read_size; i++) {
                    printf("%c", dec_message[i]);
                }
                printf("\n");

		printf("Enter message: ");
		gets(message);
                size = strlen(message);
                encrypt(message,&enc_message,0,size,key,hash);

		write(client_sock , enc_message , strlen(client_message));

		if(read_size == -1 || read_size == 0)
		{
			perror("recv failed");
			puts("Client disconnected");
			break;
		}
		fflush(stdout);

	}
	close(client_sock);
	return 0;
}
