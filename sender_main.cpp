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
#include <fstream> 
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include "window.h"


using namespace std;
#define timeout 0.5

//global variable
int		global_socket_UDP;   				//udp socket
struct addrinfo *p;
socklen_t addrlen;
struct sockaddr * address;
unordered_set<long>  			ack_state;  //insert send but not acked seqnum
unordered_map<long,struct timeval> 	time_state; //record time of sended packet
struct sockaddr_in theirAddr;
socklen_t theirAddrLen;
mutex state_mtx;
mutex ack_mtx;
mutex print_mtx;
long ack_num;
long prev_ack_num=-1;
bool start_send=false;
FILE * 	file_ptr;							//file ptr
vector<packet> file_buffer; 		//file buffer
unsigned long long int sequence_end;							//the last sequence number

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	return sa->sa_family == AF_INET
		? (void *) &(((struct sockaddr_in*)sa)->sin_addr)
		: (void *) &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/**
set up sender UDP connection
**/
void set_up_socket(char* hostname, unsigned short int hostUDPport){
	struct addrinfo hints, *servinfo;
	int rv;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	char UDPport_str[17];
	sprintf(UDPport_str, "%d", hostUDPport);
	struct timeval read_timeout;
	read_timeout.tv_sec = 0;
	read_timeout.tv_usec = 10;


	if ((rv = getaddrinfo(hostname, UDPport_str, &hints, &servinfo)) != 0) {
	fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
	return ;
	}
	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((global_socket_UDP = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("sender: socket");
			continue;
		}
		addrlen = p->ai_addrlen;
		address = p->ai_addr;
		break;
	}
	//setsockopt(global_socket_UDP,SOL_SOCKET,SO_RCVTIMEO,&read_timeout, sizeof read_timeout);
	if(p==NULL){
		fprintf(stderr, "sender: failed to connect\n");
		exit(1);
        return ;
	}
	return;
}

void send_packet(char* buffer, int len){
	int numBytes  = 0 ;
	numBytes = sendto(global_socket_UDP,buffer,len,0,address,addrlen);
	if(numBytes==-1){
		perror("sender: send failed");
		exit(1);
	}
	return ;
}



void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {
	//read file content into file packet vector
	file_buffer = read_file(filename, bytesToTransfer);
	sequence_end	= file_buffer.size();

	//set up socket connection
	set_up_socket(hostname,hostUDPport);

	window send_window(100, hostUDPport, hostname,0);
	//when window moves to the last pos and all packets are acked
	while(send_window.start<sequence_end or ack_state.empty()==false )
	{
		//send data 
		send_window.push_data(file_buffer);
		send_window.prepare_send();
		send_packet(send_window.payLoad_buffer, send_window.total_packet_size);

		//record sended packet time and state seqnum
		struct timeval now;
		gettimeofday(&now, 0);
		state_mtx.lock();
		for(int idx = send_window.start;idx<send_window.end;idx++){
			ack_state.insert(idx);
			time_state[idx] = now;
		}
		state_mtx.unlock();

		//check timeout
		long go_back =  send_window.end;

		state_mtx.lock();
		for(auto it=ack_state.begin();it!= ack_state.end();it++){
			long  not_ack_seqnum = *it;
			//if timeout 
			if( now.tv_sec - time_state[not_ack_seqnum].tv_sec > timeout){
				go_back = min(go_back,not_ack_seqnum);
				time_state[not_ack_seqnum]= now;
				print_mtx.lock();
				std::cout<<"chou sb "<<not_ack_seqnum<<std::endl;	
				print_mtx.unlock();
			}
		}
		state_mtx.unlock();

		ack_mtx.lock();
		go_back = max(go_back,ack_num+1);
		ack_mtx.unlock();
		//slide whole window to next position that this round's start = previous round's end
		print_mtx.lock();
		std::cout<<"next start "<< go_back<<std::endl;	
		print_mtx.unlock();

		send_window.start= go_back;
		send_window.end = min(send_window.start+send_window.window_size, sequence_end);
		send_window.window_size = send_window.end - send_window.start;
	}

	//send ending signal
	for(int i=0;i<20;i++)
	{
		packet finish(2);
		char* endbuf = packet_to_char(&finish);
		send_packet(endbuf, packet_buffer_size);
		delete endbuf;
	}
	std::cout<<"send finished and total packets are "<<file_buffer.size()<<std::endl;
	close(global_socket_UDP);
}

void* receive_ack(void* hh){
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 10 * 1000 * 1000; //10 ms
	//wait for ack
	int bytesRecvd;
	char recvBuf[50000];
	memset(recvBuf, '\0', 50000);
	theirAddrLen = sizeof(theirAddr);

	while(1){
		if ((bytesRecvd = recvfrom(global_socket_UDP, recvBuf, 50000, 0, 
		(struct sockaddr*)&theirAddr, &theirAddrLen)) == -1){
		//perror(" receiving ack failed");
		//exit(1);
		}

		//get ack sequence number and ack packet and remove acked seqnum
		if(bytesRecvd !=0 and bytesRecvd !=-1){
			packet* ack_pkt = char_to_packet(recvBuf);
			long ack_until = ack_pkt->seqnum;
			delete ack_pkt;

			print_mtx.lock();
			//std::cout<<"real received ack: "<<ack_until<<std::endl;
			print_mtx.unlock();

			if(prev_ack_num!=ack_until){
				state_mtx.lock();
				for(auto it=ack_state.begin();it!= ack_state.end();){
					long wait_for_ack= *it;
					if(wait_for_ack <= ack_until){
						//it = ack_state.erase(it);
						ack_state.erase(it++);
						time_state.erase(wait_for_ack);
					}
					else
					{
						++it;
					}
				}
				state_mtx.unlock();

				ack_mtx.lock();
				prev_ack_num = ack_num;
				ack_num = ack_until;
				ack_mtx.unlock();
			}
		}

	}



}


int main(int argc, char** argv)
{
	unsigned short int udpPort;
	unsigned long long int numBytes;
	
	if(argc != 5)
	{
		fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
		exit(1);
	}
	udpPort = (unsigned short int)atoi(argv[2]);
	numBytes = atoll(argv[4]);
	
	std::cout<<"sending file "<<argv[3]<<" bytes of "<<numBytes<<" bytes to receiver "<<argv[1]<<":"<<udpPort<<std::endl;
	pthread_t receiveThread;
	pthread_create(&receiveThread, 0, receive_ack, (void*)0);
	reliablyTransfer(argv[1], udpPort, argv[3], numBytes);

} 
