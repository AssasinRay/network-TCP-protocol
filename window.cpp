#include "window.h"
//std::cout<<"ca"<<std::endl;



/***

debug function

****/

//print timeval struct in a formatted way
void print_time(struct timeval tv ){
	time_t nowtime;
	struct tm *nowtm;
	char tmbuf[64], buf[64];
	nowtime = tv.tv_sec;
	nowtm = localtime(&nowtime);
	strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
	snprintf(buf, sizeof buf, "%s.%06ld", tmbuf, tv.tv_usec);
	puts(buf);
}

//print current window information
void print_window(window slide_window){
	std::cout<<"start: "<<slide_window.start<<std::endl;
	std::cout<<"end: "<<slide_window.end<<std::endl;
	std::cout<<"size: "<<slide_window.window_size<<std::endl;
	for(int i=0;i<slide_window.window_size;i++)
	{
		if(slide_window.data[i] == NULL) continue;
		std::cout<<"packet id: "<<(slide_window.data[i])->seqnum<<std::endl;
		std::cout<<"ack state: "<<slide_window.acknowledge[i]<<std::endl;
		print_time(slide_window.times[i]);
	}
	std::cout<<std::endl;
}

//print a packet
void print_packet(packet* pkt){
	std::cout<<"packet header: "<<pkt->header<<std::endl;
	std::cout<<"packet data size: "<<pkt->data_size<<std::endl;
	std::cout<<"packet id: "<<pkt->seqnum<<std::endl;
	for(int i=0;i<packet_size;i++)
	{
		std::cout<<pkt->data[i];
	}
	std::cout<<std::endl;
	return;
}


void print_vector(std::vector<unsigned long long int>& vec){
	for(int i=0;i<vec.size();i++)
	{
		std::cout<<vec[i]<<std::endl;
	}
	std::cout<<std::endl;
	return;
}

unsigned long long int get_file_size(char* file_name){
	FILE* fp = fopen(file_name,"rb");
	fseek(fp, 0L, SEEK_END);
	unsigned long long int sz = ftell(fp);
	fclose(fp);
	return sz;
}

/***
transfer a file of bytes To Transfer into a vector<packet>
***/
std::vector<packet> read_file(char* filename, unsigned long long int bytesToTransfer){
	FILE* file_ptr = fopen(filename,"rb");
	fseek(file_ptr, 0L, SEEK_SET);
	int packet_num = bytesToTransfer/packet_size+1;
	std::vector<packet> res;
	char byte ;

	for(int i=0,j=0;i<packet_num;i++,j+=packet_size)
	{
		uint8_t buffer[packet_size];
		memset(buffer,'\0',packet_size);
		size_t  size;
		//check last  bytes
		if(j+packet_size > bytesToTransfer){
			size = fread(buffer,1, bytesToTransfer-j ,file_ptr);
		}
		else{
			 size = fread(buffer,1,packet_size,file_ptr);
		}
		packet cur(1,i,buffer, size);
		res.push_back(cur);
	}
	fclose(file_ptr);
	return res;
}

/***
read a vector<packet> and write the file to the disk
**/
void write_file(std::vector<packet>& packet_vec, char* destinationFile){
	std::ofstream outfile(destinationFile);
	for(int i=0;i<packet_vec.size();i++){
		outfile.write((const char*)packet_vec[i].data, packet_vec[i].data_size);
	}
	return;
}




/***

member function

****/



/***
constructor of window
**/
window::window(int size, int port_num, char* host_name,int start_pos){
	this->window_size 	= size;
	this->port 			= port_num;
	this->host 			= host_name;
	this->start 		= start_pos;
	this->end   		= start_pos+size;
	this->data 			= std::deque<packet*>(size,NULL);
	this->acknowledge   = std::deque<bool>(size,false);
	this->times         = std::deque<struct timeval>(size);
	this->total_packet_size=0;
	memset(payLoad_buffer,'\0',payLoad_buffer_size);
	return;
}

window::window(char* host_name){
	this->window_size 	= 10;
	this->port 			= 0;
	this->host 			= host_name;
	this->start 		= 0;
	this->end   		= 10;
	this->data 			= std::deque<packet*>(window_size,NULL);
	this->acknowledge   = std::deque<bool>(window_size,false);
	this->times         = std::deque<struct timeval>(window_size);
	this->total_packet_size=0;
	memset(payLoad_buffer,'\0',payLoad_buffer_size);
	return;
}

/***
push window sized data into window it also check tiny data 
**/
void window::push_data(std::vector<packet>& file_packet_vec){
	if(file_packet_vec.size() < window_size) shrink_window(file_packet_vec.size());

	for(int i=0;i<window_size;i++)
	{
		int idx = get_seq_num(i);
		if(idx==-1){
			std::cout<<"push data failed"<<std::endl;
			return;
		}
		this->data[i] = &file_packet_vec[idx];
	}
}

/***
slide window for one step for sender's window
***/
void window::sender_slide(std::vector<packet>& file_packet_vec){
	this->data.pop_front();
	this->acknowledge.pop_front();
	this->times.pop_front();
	this->start++;
	unsigned long long int seq_end = file_packet_vec.size() -1;
	if(this->end <= seq_end){
		this->data.push_back(&file_packet_vec[this->end]);
		this->end++;
		this->acknowledge.push_back(false);
		struct timeval now;
		this->times.push_back(now);
	}
	window_size = end- start;
	return;
}

 long long int window::receiver_slide(std::vector<packet>& file_packet_vec){
	//std::cout<<start<<std::endl;
	long long int ack_seq = -1;
	while(this->data[0] != NULL){
		file_packet_vec.push_back(*(this->data[0]));
		ack_seq = this->data[0]->seqnum;
		delete this->data[0];
		this->data.pop_front();
		this->data.push_back(NULL);
		start++;
		end++;
	}
	return ack_seq;
} 



/***
encode a packet to char buffer  buffer[0-3] = header, buffer[4-7] = data size , buffer[8-15] = seqnum
buffer[16-end] = data content 
dont forget to delete!
***/
char* packet_to_char(packet* pck){
	int pck_strcture_size = 4+4+8+packet_size;
	char*   res = new char[pck_strcture_size+1];	
	memcpy(res, &pck->header, sizeof(int));
	//std::cout<<*(int*)res<<std::endl;
	memcpy(res+4, &pck->data_size, sizeof(int));
	//std::cout<<*(int*)(res+4)<<std::endl;
	memcpy(res+8, &pck->seqnum, sizeof(unsigned long long int));
	//std::cout<<*(unsigned long long int*)(res+8)<<std::endl;
	for(int i=0;i<pck->data_size;i++)
	{
		res[i+16] = (char)pck->data[i];
		//std::cout<<(char)res[i+16];
	}
	res[pck_strcture_size] = '\0';
	return res;
}

/******/
packet* char_to_packet(char* buffer){
	int header = *(int*)buffer;
	//std::cout<< header<<std::endl;
	int data_size = *(int*)(buffer+4);
	//std::cout<< data_size<<std::endl;
	unsigned long long int seq_num = *(unsigned long long int*)(buffer+8);
	//std::cout<< seq_num<<std::endl;
	packet *res = new packet(header,seq_num,(uint8_t*)buffer+16,data_size);
	return res;
}


/***
prepare window->data into char pay load buffer so that sender can send it
***/
void window::prepare_send(){
	//write all the packets into buffer
	int pck_strcture_size = 4+4+8+packet_size+1;
	this->total_packet_size = 0;
	memset(payLoad_buffer,'\0',payLoad_buffer_size);
	for(int i=0;i<window_size;i++){
		packet* temp = this->data[i];
		char *sub = packet_to_char(this->data[i]);
		memcpy(payLoad_buffer+i*pck_strcture_size,sub,pck_strcture_size);
		this->total_packet_size+=pck_strcture_size;
		delete sub;
	}
	return;
}

/***
recv char buffer and convert it to packet vector
***/
std::vector<unsigned long long int>  window::prepare_recv(char* buffer, int numbytes,std::vector<packet>& output_buffer){
	int pck_strcture_size = 4+4+8+packet_size+1;
	std::vector<unsigned long long int>  res;
	for(int i=0;i<numbytes;i+=pck_strcture_size){
		packet* pkt = char_to_packet(buffer + i);
		if(pkt->header == 2){
			delete pkt;
			res.push_back(1844674407370955161);
			return res;
		}
		unsigned long long int seq_num = pkt->seqnum;
		res.push_back(seq_num);
		//if(output_buffer[seq_num]!=NULL) continue;
		output_buffer[seq_num] = *(pkt);
		delete pkt;
	}
	return res;
}

int window::prepare_recv(char* buffer, int numbytes){
	int pck_strcture_size = 4+4+8+packet_size+1;
	for(int i=0;i<numbytes;i+=pck_strcture_size){
		packet* pkt = char_to_packet(buffer + i);
		if(pkt->header == 2){
			delete pkt;
			return 2;
		}
		unsigned long long int seq_num = pkt->seqnum;
		int idx = get_idx(seq_num);

		//std::cout<<idx<<std::endl;
		if(idx!=-1) data[idx] = pkt;
	}
	return 1;
}
void window::prepare_ack(std::vector<unsigned long long int>& seq_num_vec){
	int pck_strcture_size = 4+4+8+packet_size+1;
	this->total_packet_size = 0;
	memset(payLoad_buffer,'\0',payLoad_buffer_size);
	for(int i=0;i<seq_num_vec.size();i++){
		unsigned long long int sequence_num = seq_num_vec[i];
		packet ack(0,sequence_num);
		char*  ack_buf = packet_to_char(&ack);
		memcpy(payLoad_buffer+i*pck_strcture_size,ack_buf,pck_strcture_size);
		this->total_packet_size+=pck_strcture_size;
		delete ack_buf;
	}
	return;
}

std::vector<unsigned long long int> window::prepare_recv_ack(char* buffer, int numbytes){
	int pck_strcture_size = 4+4+8+packet_size+1;
	std::vector<unsigned long long int> res;
	for(int i=0;i<numbytes;i+=pck_strcture_size){
		packet* pkt = char_to_packet(buffer + i);
		unsigned long long int seq_num = pkt->seqnum;
		res.push_back(seq_num);
		delete pkt;
	}
	return res;
}
/***
resend one of idx'th packet
***/
void window::reprepare_packet(packet* pkt){

	//write this packet into buffer
	int pck_strcture_size = 4+4+8+packet_size+1;
	this->total_packet_size = 0;
	memset(payLoad_buffer,'\0',payLoad_buffer_size);
	char *sub = packet_to_char(pkt);
	memcpy(payLoad_buffer,sub,pck_strcture_size);
	this->total_packet_size+=pck_strcture_size;
	delete sub;

	return;
}

/***
resend one of idx'th packet
***/
void window::reprepare_packet_vec(std::vector<unsigned long long int>& not_ack_seqnum_vec, std::vector<packet>& file_buffer){
	int pck_strcture_size = 4+4+8+packet_size+1;
	this->total_packet_size = 0;
	memset(payLoad_buffer,'\0',payLoad_buffer_size);
	for(int i=0;i<window_size and not_ack_seqnum_vec.empty()==false;i++){
		unsigned long long int seqnum = not_ack_seqnum_vec.back();
		not_ack_seqnum_vec.pop_back();
		packet* temp = &file_buffer[seqnum];
		char *sub = packet_to_char(temp);
		memcpy(payLoad_buffer+i*pck_strcture_size,sub,pck_strcture_size);
		this->total_packet_size+=pck_strcture_size;
		delete sub;
	}
	return;
}

void window::update_packet_time(){
	for(int i=0;i<this->window_size;i++){
		struct timeval now;
		gettimeofday(&now, 0);
		this->times[i] = now;
	}
	return;
}

/***
check the sequence number inside the window range
**/
bool window::inside_window(int seq_num){
	if(seq_num>= end or seq_num<start) return false;
	return true;
}

/***
check the sequence number bigger than window end
**/
bool window::outside_window(int seq_num){
	if(seq_num>= end) return true;
	return false;
}

/***
get the corresponding index of the window from sequence number in the window
if not inside window then return -1
**/
int window::get_idx(int seq_num){
	if(inside_window(seq_num) == false) return -1;
	return seq_num - start;
}

/***
get the corresponding sequence number from window index
if not inside window then return -1
**/
int window::get_seq_num(int idx){
	if(idx<0 or idx>=window_size) return -1;
	return idx+start;
}

/***
set the corresponding packet ack state = true
**/
void window::ack_packet(int seq_num){
	int idx = get_idx(seq_num);
	if(idx==-1){
		std::cout<<"invalid seq number"<<std::endl;
		return;
	}
	acknowledge[idx]=true;
	return;
}

/***
set the packet ack state = true until seq num
**/
void window::ack_until_seq(int seq_num){
	for(int i=start;i<=seq_num;i++){
		ack_packet(i);
	}
	return;
}

/**
shrink window to new size
***/
void window::shrink_window(int new_size){
	if(new_size>=window_size) return;
	window_size = new_size;
	for(int i=new_size;i<end;i++)
	{
		data.pop_back();
		acknowledge.pop_back();
		times.pop_back();
	}
	end = start+new_size;
}
/*
int main()
{
	// FILE* fp = fopen("1.jpg","rb");
	// fseek(fp, 0L, SEEK_END);
	// unsigned long long int sz = ftell(fp);

	unsigned long long int sz = get_file_size("1.jpg");
	std::cout<<sz<<std::endl;
	std::vector<packet> cao = read_file("1.jpg", sz);
	unsigned long long int seq_end = cao.size();
	std::cout<<seq_end<<std::endl;

	std::vector<packet> receive;

	window::window s(5, 5, "",0);
	window::window r(5, 5, "",0);
	s.push_data(cao);

	for(int seqnum =0; seqnum<seq_end; seqnum++)
	{
		if(seqnum%5==0){
			s.prepare_send();
			r.prepare_recv(s.payLoad_buffer,s.total_packet_size);
			r.receiver_slide(receive);
		}
		s.sender_slide(cao);
	}


	write_file(receive,"paidaxing");

	//write_file(cao);
	return 0;
}*/

