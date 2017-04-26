#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <mutex>
#include <fstream> 
#include <pthread.h>
#include "window.h"

//using namespace std;
int		global_socket_UDP;   				//udp socket
struct addrinfo *p;
std::mutex ack_mtx;
socklen_t theirAddrLen;
struct sockaddr  theirAddr;
long ack_until =-1 ;
std::mutex print_mtx;
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	return sa->sa_family == AF_INET
		? (void *) &(((struct sockaddr_in*)sa)->sin_addr)
		: (void *) &(((struct sockaddr_in6*)sa)->sin6_addr);
}
/***
set up the UDP port
***/
void set_up_socket(unsigned short int hostUDPport){
	struct addrinfo hints, *servinfo;
	int rv;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP
	char UDPport_str[17];
	sprintf(UDPport_str, "%d", hostUDPport);
	struct timeval read_timeout;
	read_timeout.tv_sec = 0;
	read_timeout.tv_usec = 10;

	if ((rv = getaddrinfo(NULL, UDPport_str, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return ;
	}
	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((global_socket_UDP = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("receiver: socket");
			continue;
		}
		if (bind(global_socket_UDP, p->ai_addr, p->ai_addrlen) == -1){
			close(global_socket_UDP);
			perror("receiver: bind");
			continue;
		}
		break;
	}

	//setsockopt(global_socket_UDP,SOL_SOCKET,SO_RCVTIMEO,&read_timeout, sizeof read_timeout);

	if (p == NULL)  {
		fprintf(stderr, "receiver: failed to bind\n");
		return ;
	}
	freeaddrinfo(servinfo); // all done with this structure
	std::cout<<"receiver: waiting for UDP connection ...."<<std::endl;
	return;
}

void send_packet(char* buffer, int len){
	int numBytes;
	numBytes = sendto(global_socket_UDP,buffer,len,0,(struct sockaddr *)&theirAddr,theirAddrLen);
	if(numBytes==-1){
		perror("acker: send failed");
		exit(1);
	}
	return ;
}

void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {
	set_up_socket(myUDPport);

	//std::vector<packet> output_buffer(10000000);
	std::vector<packet> output_buffer;
	//things for receiving acks
	int bytesRecvd;
	unsigned long long int totalbytes=0;
	char recvBuf[50000];
	memset(recvBuf, '\0', 50000);
	bool send_finished=false;
	window recv_window(100, 0, "char* host_name",0);
	while(1){
		//receive data
		theirAddrLen = sizeof(theirAddr);
		if ((bytesRecvd = recvfrom(global_socket_UDP, recvBuf, 50000, 0, 
			(struct sockaddr*)&theirAddr, &theirAddrLen)) == -1){
			perror("receiver receiving data failed");
			exit(1);
		}
		totalbytes+=bytesRecvd;

		//check finish
		int header = recv_window.prepare_recv(recvBuf, bytesRecvd);
		if(header==2) break;

		long long seq_end = recv_window.receiver_slide(output_buffer);
		if(seq_end!=-1){
			ack_mtx.lock();
			print_mtx.lock();
			//std::cout<<"slide: "<<ack_until<<std::endl;
			print_mtx.unlock();
			ack_until = seq_end;
			ack_mtx.unlock();
		}
	}

	std::cout<<"finish receiving file and total packets are "<<ack_until+1<<std::endl;
	//output_buffer.resize(ack_until+1);
	write_file(output_buffer,destinationFile);
	close(global_socket_UDP);
}


void* send_ack(void* hh){
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 100 * 1000 * 1000; //100 ms

	while(1){
		ack_mtx.lock();
		long ack_copy = ack_until;
		ack_mtx.unlock();
		if(ack_copy==-1) continue;

		packet* ack_pkt= new packet(0,ack_copy);

		char* ack_buffer=packet_to_char(ack_pkt);
		send_packet(ack_buffer,packet_buffer_size);
		delete ack_pkt;
		delete ack_buffer;

		print_mtx.lock();
		std::cout<<"real send ack: "<<ack_copy<<std::endl;
		print_mtx.unlock();
		nanosleep(&sleepFor, 0);
	}
}

int main(int argc, char** argv)
{
	unsigned short int udpPort;
	
	if(argc != 3)
	{
		fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
		exit(1);
	}
	

	udpPort = (unsigned short int)atoi(argv[1]);
	std::cout<<"receive file in port "<<udpPort<<std::endl;

	pthread_t sendThread;
	pthread_create(&sendThread, 0, send_ack, (void*)0);
	reliablyReceive(udpPort, argv[2]);
}
