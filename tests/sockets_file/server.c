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
	char *client_ip;

        uint8_t enc_file[1000], *dec_file, *hash;
        uint8_t key[16] = {0x10, 0xa5, 0x88, 0x69, 0xd7, 0x4b, 0xe5, 0xa3, 0x74, 0xcf, 0x86, 0x7c, 0xfb, 0x47, 0x38, 0x59};
        int size, i;

        hash = (uint8_t *)malloc(16*sizeof(uint8_t));

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
	if (read_size = recv(client_sock , enc_file , 1024 , 0) <= 0){
		perror("recv failed");
		puts("Client disconnected");
		return 0;
	}
// COMO RESERVAS MEM SINO SABES CUANTO VAS A RECIBIR ??
        dec_file = (uint8_t *)malloc(read_size*sizeof(uint8_t));

        decrypt(enc_file,&dec_file,0,read_size,key,&hash);

        printf("%s :\n",client_ip);
        printf("File decrypted");

	fflush(stdout);
	close(client_sock);
	return 0;
}
