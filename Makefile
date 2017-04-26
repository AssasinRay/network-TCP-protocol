all: reliable_sender reliable_receiver

reliable_sender:sender_main.cpp window.cpp window.h
	g++ -g -pthread -std=c++11 -o reliable_sender sender_main.cpp window.cpp 

reliable_receiver:receiver_main.cpp window.cpp window.h
	g++ -g -pthread -std=c++11 -o reliable_receiver receiver_main.cpp window.cpp 

clean:
	rm reliable_sender reliable_receiver
