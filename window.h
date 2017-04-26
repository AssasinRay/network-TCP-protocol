
#include <queue>
#include "math.h"
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
#include <sys/time.h>

#define packet_size 128
#define payLoad_buffer_size 2560000
#define MIN( a, b ) ( ( a < b) ? a : b ) 
#define packet_buffer_size 4+4+8+packet_size+1
inline int min ( int a, int b ) { return a < b ? a : b; }

//total size = 4+4+8+packet_size bytes
class packet{
public:
	int header; 						//0 is ack, 1 is normal, 2 is fin
	unsigned long long int seqnum;  						// sequece num
	int data_size;
	uint8_t data[packet_size]; 			//data 

	packet(){}
	//pck for finish
	packet(int header_type){
		header = header_type;
		seqnum = -1;
		data_size=0;
		memset(data,'\0',packet_size);
	}
	//pck for ack
	packet(int header_type,unsigned long long int seq){
		header = header_type;
		seqnum = seq;
		data_size=0;
		memset(data,'\0',packet_size);
	}
	//pck for data
	packet(int header_type, unsigned long long int seq, uint8_t input_data[], unsigned int bytes_num)
	{
		header = header_type;
		seqnum = seq;
		data_size = bytes_num;
		if(bytes_num<packet_size) memset(data,'\0',packet_size);
		memcpy(data, input_data, bytes_num);
	}
};

class window{
public:
	int window_size;
	char* host;
	int port;
	int start ;
	int end   ;			//deque range = [start,end-1] does not include end index
	int total_packet_size;
	char payLoad_buffer[payLoad_buffer_size];

	std::deque<packet*> data;
	std::deque<bool>   acknowledge;
	std::deque<struct timeval> times;

	window(char* host_name);
	window(int size, int port_num, char* host_name,int start_pos); 		//constructor
	void push_data(std::vector<packet>& file_packet_vec);				//push data into window
	int get_idx(int seq_num);											//get corresponding index in window of a seq num return -1 if not inside window
	int get_seq_num(int idx);											//get seq num based on idx
	void ack_packet(int seq_num); 										//ack one packet
	void ack_until_seq(int seq_num);									//ack until seq_num
	bool inside_window(int seq_num);									//check the seq num inside the window
	bool outside_window(int seq_num);									//packet out of window end
	void sender_slide(std::vector<packet>& file_packet_vec) ;					//slide window for sender
	 long long int receiver_slide(std::vector<packet>& file_packet_vec);
	void shrink_window(int new_size);
	void increase_window();
	void prepare_send();												//send whole data to receiver
	std::vector<unsigned long long int> prepare_recv(char* buffer, int numbytes,std::vector<packet>& output_buffer);						//receive data and transfer into window data
	void reprepare_packet(packet* pkt);
	void update_packet_time();
	void prepare_ack(std::vector<unsigned long long int>& seq_num_vec);
	std::vector<unsigned long long int> prepare_recv_ack(char* buffer, int numbytes);
	void reprepare_packet_vec(std::vector<unsigned long long int>& not_ack_seqnum_vec, std::vector<packet>& file_buffer);
	int prepare_recv(char* buffer, int numbytes);
};


std::vector<packet> read_file(char* filename, unsigned long long int bytesToTransfer);
void print_time(struct timeval tv );
void print_window(window slide_window);
void print_packet(packet* pkt);
void print_vector(std::vector<unsigned long long int>& vec);
unsigned long long int get_file_size(char* file_name);
void write_file(std::vector<packet>& packet_vec, char* destinationFile);
char* packet_to_char(packet* pck);
packet* char_to_packet(char* buffer);

