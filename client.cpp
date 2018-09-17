#include <iostream>
#include <stdio.h>
#include <sys/types.h>   
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h> 
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>
#include <list>
#include <netdb.h>
#include "globals.h"
using namespace std;

int client_seq;
int acknum;
int fd;						  // for file received.data
segment buf;
int filename_len = 0;
int fileSize = 1;	  // DO NOT CHANGE! Size of file being sent from server
int amtReceived = 0;  // Amount of bytes received from server
bool receivedFIN = false;

void connectToServer(int socknum, struct sockaddr_in server, char *filename) {
    ////////////////////////////////////////////////////////////////////
    // Part 1: Send SYN and client's initial seq # */
    ////////////////////////////////////////////////////////////////////
	clock_t start; //timer
	double duration = 0; //elapsed time begins at zero
	fd_set working_set; //select structures
	struct timeval tv;

	start = clock(); //note current time
	int max_sd, rc;
	max_sd = socknum; //largest file descriptor in fd_set
	FD_ZERO(&working_set);
	FD_SET(socknum, &working_set); //include socknum in the working set of file descriptors
	tv.tv_sec = 0; //initialize timeout to zero == non-blocking select
	tv.tv_usec = 0;
	double clk_per_ms = (double)CLOCKS_PER_SEC / 1000; //number of clocks per millisecond

	memset(&buf, 0, sizeof(segment));
	buf.seq = client_seq;
	buf.ack = 0;   // this acknum doesn't matter
	buf.connection = SYN;
    buf.length = HEADER_SIZE;

    SendtoS(socknum, &buf, buf.length, 0, (struct sockaddr *) &server, sizeof(server), 0);
    
	int server_len = sizeof(server);
	segment buf2;

	while (1) {
		rc = Select(max_sd + 1, &working_set, NULL, NULL, &tv); //determine if socknum has any data to read
		
		if (duration >= RTO) { //resend packet after a time out
			SendtoS(socknum, &buf, buf.length, 0, (struct sockaddr *) &server, sizeof(server), 1);
			start = clock(); //reset clock
		}

		if (rc) { //read from socket if data is available
			bool valid;
			valid = Recvfrom(socknum, &buf2, PKT_SIZE, 0, (struct sockaddr *) &server, (socklen_t *)&server_len);
			if (valid && buf2.ack == client_seq + 1 && buf2.connection == SYN) { //check for correct ack from server
				acknum = buf2.seq;
				Wrap_inc(acknum, 1);
				printReceive(acknum);
				break;
			}
			else {
				acknum = buf2.seq;
				printReceive(acknum);
			}
		}

		duration = (clock() - start) / clk_per_ms; //increase duration to elapsed milliseconds
		FD_ZERO(&working_set);
		FD_SET(socknum, &working_set); //must re-include socknum in the working set of file descriptors
	}

    ////////////////////////////////////////////////////////////////////
    // Part 3: ACK server's SYN + Piggyback File Request
    ////////////////////////////////////////////////////////////////////
    
    Wrap_inc(client_seq, 1);

    /* Send ACK for server's SYN + file request */
    memset(&buf, 0, sizeof(segment));
    buf.seq = client_seq;
    buf.ack = acknum;
    strcpy(buf.msg, filename);
    buf.length = filename_len + HEADER_SIZE;
    Wrap_inc(client_seq, buf.length - HEADER_SIZE);
}

void CloseConnection(int socknum, struct sockaddr_in &server) {
	int server_len = sizeof(server);
	segment pkt, rcvPkt;

	clock_t start; //timer
	double duration = 0; //elapsed time begins at zero
	fd_set working_set; //select structures
	struct timeval tv;
	int max_sd, rc;
	max_sd = socknum; //largest file descriptor in fd_set
	FD_ZERO(&working_set);
	FD_SET(socknum, &working_set); //include socknum in the working set of file descriptors
	tv.tv_sec = 0; //initialize timeout to zero == non-blocking select
	tv.tv_usec = 0;
	double clk_per_ms = (double)CLOCKS_PER_SEC / 1000; //number of clocks per millisecond

	// Send FIN-ACK (Combine FIN and ACK as the spec says)
	pkt.ack = acknum;
	pkt.connection = FIN;
	pkt.seq = client_seq;
	pkt.length = HEADER_SIZE;
	SendtoS(socknum, &pkt, pkt.length, 0, (struct sockaddr *) &server, sizeof(server), 0); //ack separated from fin for shutdown

	Wrap_inc(client_seq, 1); // FIN is 1 byte

	start = clock(); //note current time
	// Keep retransmitting FIN-ACK until final ACK arrives or TIME_WAIT finishes
	int count, max;
	max = 2*RTO; //just so we dont loop forever if final ack is lost
	count = 0;
	bool receivedFinalACK = false;
	while (1) {
		rc = select(max_sd + 1, &working_set, NULL, NULL, &tv); //determine if socknum has any data to read
		if (rc == -1) {
			fprintf(stderr, "Select function failed. Error value is: %s\n", strerror(errno));
			exit(1);
		}

		if (duration >= RTO) { //resend packet after a time out NOT SURE IF WE SHOULD DO THIS
			count += RTO;
			 if (count >= max) { //break infinite loop
			 	break;
			 }
			if (!receivedFinalACK)
				SendtoS(socknum, &pkt, pkt.length, 0, (struct sockaddr *) &server, sizeof(server), 1);
			start = clock(); //reset clock
		}

		if (rc) { //read from socket if data is available
			Recvfrom(socknum, &rcvPkt, PKT_SIZE, 0, (struct sockaddr *) &server, (socklen_t *)&server_len);
			printReceive(acknum);
			if (rcvPkt.seq == acknum)
				receivedFinalACK = true;
		}

		duration = (clock() - start) / clk_per_ms; //increase duration to elapsed milliseconds
		FD_ZERO(&working_set);
		FD_SET(socknum, &working_set); //must re-include socknum in the working set of file descriptors
	}
}

/** Continually ACKs server's FTP responses and saves the file onto local directory. */
void receiveFile(int fd, int socknum, struct sockaddr_in &server) {
	fd_set working_set; //select structures
	struct timeval tv;

	int max_sd, rc;
	max_sd = socknum; //largest file descriptor in fd_set
	FD_ZERO(&working_set);
	FD_SET(socknum, &working_set); //include socknum in the working set of file descriptors
	tv.tv_sec = 0; //initialize timeout to zero == non-blocking select
	tv.tv_usec = 0;

	int server_len = sizeof(server);

	clock_t start; //timer
	double duration = 0; //elapsed time begins at zero
	start = clock(); //note current time
	double clk_per_ms = (double)CLOCKS_PER_SEC / 1000; //number of clocks per millisecond

	bool file_request = true;
	list<segment> rcvPackets;
	segment pkt, rcvPkt;
	bool resend_ack = false;
	bool valid;
	int base_seq = client_seq;
	SendtoS(socknum, &buf, buf.length, 0, (struct sockaddr *) &server, sizeof(server), 0);

	int count, max;
	max = 10*RTO; //just so we dont loop forever if final ack is lost
	count = 0;
	bool start_counting = false;
	while (1) {
		/* Select: See if a packet has arrived at the socket. */
		rc = Select(max_sd + 1, &working_set, NULL, NULL, &tv); //determine if socknum has any data to read

		if (duration >= RTO && file_request) { //resend packet after a time out
			SendtoS(socknum, &buf, buf.length, 0, (struct sockaddr *) &server, sizeof(server), 1);
			start = clock(); //reset clock
		}

		if (start_counting && duration >= RTO) {
			count += RTO;
			if (count >= max && amtReceived >= fileSize) {  // If FIN doesn't arrive for a long time but we received all data, exit
				break;
			}
			start = clock();
		}

		if (amtReceived >= fileSize && !start_counting) {
			start = clock();
			start_counting = true;
		}


		if (rc) { //read from socket if data is available
			resend_ack = true; //set to true by default
			int diff = acknum + 4000; //largest possible received sequence number is 4000 greater than base acknum
			int end_win = diff;
			Wrap_inc(end_win, 0);
    		memset(&rcvPkt, 0, sizeof(segment));
			valid = Recvfrom(socknum, &rcvPkt, PKT_SIZE, 0, (struct sockaddr *) &server, (socklen_t *)&server_len);
			if (!(valid && rcvPkt.seq == acknum))
				printReceive(acknum);
			// Received the packet we're expecting
			if (valid && rcvPkt.seq == acknum) { //if the next packet we get is the base sequence number of client window
				// If we receive FIN or detect that we already got all the data bytes from the server, stop
				if (rcvPkt.connection == FIN) {
					Wrap_inc(acknum, 1);
					printReceive(acknum);
					receivedFIN = true;
					break;
				}
				if (file_request) {
					file_request = false;
					fileSize = rcvPkt.fileSize;
				}
				resend_ack = false; //not resending ack for base sequence number
				rcvPackets.push_front(rcvPkt);

				list<segment>::iterator seg_pointer, end_pointer;
				seg_pointer = rcvPackets.begin();
				end_pointer = rcvPackets.end();
				bool increment = true;
				while (seg_pointer != end_pointer) { //if we have packets buffered that can now be written
					increment = true;
					if (seg_pointer->seq == acknum) { //iterate through list and check for matches
						if (write(fd, seg_pointer->msg, seg_pointer->length - HEADER_SIZE) == -1) {
							fprintf(stderr, "Error: System call write failed. Error value is %s.\n", strerror(errno));
							exit(1);
						}
						amtReceived += seg_pointer->length - HEADER_SIZE;
						Wrap_inc(acknum, seg_pointer->length - HEADER_SIZE);
						rcvPackets.erase(seg_pointer); //remove that packet from list
						seg_pointer = rcvPackets.begin();
						increment = false;
					}
					if (increment)
						seg_pointer++;
				}
				printReceive(acknum);
				//Wrap_inc(client_seq);
				pkt.seq = client_seq;
				pkt.ack = acknum;
				pkt.length = HEADER_SIZE;
				SendtoS(socknum, &pkt, pkt.length, 0, (struct sockaddr *) &server, sizeof(server), 0);
				base_seq = client_seq;
			}
			// Buffer out of order packets
			else if (valid && ((rcvPkt.seq > acknum && rcvPkt.seq <= acknum + MAX_CWND) || ((diff > MAX_SEQ) && (rcvPkt.seq <= end_win)))) {
				list<segment>::iterator seg_pointer, end_pointer;
				seg_pointer = rcvPackets.begin();
				end_pointer = rcvPackets.end();
				bool dup = false;
				while (seg_pointer != end_pointer) {
					if (seg_pointer->seq == rcvPkt.seq)
						dup = true;
					seg_pointer++;
				}
				if (!dup) {
					rcvPackets.push_back(rcvPkt);
				}
			}
			if (resend_ack) { //sending ack for base window sequence number again because it has not yet been received
				pkt.seq = base_seq;
				//Wrap_inc(client_seq);
				pkt.ack = acknum;
				pkt.length = HEADER_SIZE;
				if (file_request)
					SendtoS(socknum, &buf, buf.length, 0, (struct sockaddr *) &server, sizeof(server), 1);
				else
					SendtoS(socknum, &pkt, pkt.length, 0, (struct sockaddr *) &server, sizeof(server), 1);
			}
		}
		duration = (clock() - start) / clk_per_ms; //increase duration to elapsed milliseconds
		FD_ZERO(&working_set);
		FD_SET(socknum, &working_set); //must re-include socknum in the working set of file descriptors
	}
	
}


int main(int argc, char **argv){
    /* Assert correct command line args. */
    if (argc != 4) {
        fprintf(stderr, "Incorrect command line args.\n"
                "usage: %s [hostname] [port] [filename]\n", argv[0]);
        exit(1);
    }

    filename_len = strlen(argv[3]);
    char *filename = (char *) malloc(filename_len + 1);
	strcpy(filename, argv[3]);
    filename[filename_len] = '\0';

    /* Initialize the UDP socket used to communicate with server. */
    int socknum = socket(AF_INET, SOCK_DGRAM, 0);
    if (socknum == -1){
        fprintf(stderr, "System call socket failed. Error value is: %s\n", strerror(errno));
        exit(1);
    }

    /* Initialize server address and port number. */
    int portnum = atoi(argv[2]);
    struct sockaddr_in server;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_family = AF_INET;
    server.sin_port = htons(portnum);

    // Set up file to write to
	fd = creat("received.data", 00666);
	if (fd < 0) {
		fprintf(stderr, "Error: unable to open file to write to. File is %s. Error value is %s.\n", filename, strerror(errno));
		exit(1);
	}

	srand(time(0));
	client_seq = rand() % 30001;

    /* Send TCP connection request. */
    connectToServer(socknum, server, filename);


    /* Receive file. */
	receiveFile(fd, socknum, server);

	int r = close(fd);
	if (r == -1) {
		fprintf(stderr, "System call close failed. Error value is %s\n", strerror(errno));
		exit(1);
	}

    if (receivedFIN)
		CloseConnection(socknum, server);

	if (filename) free(filename);
}
