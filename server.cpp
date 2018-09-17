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
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>
#include <cstdio>
#include <ctime>
#include <sys/select.h>
#include <queue>
#include <cstdlib>
#include "globals.h"
using namespace std;

int serv_seq;
int expected_ack;
char *file; // requested filename
int cwnd;
int ssthresh;
int fileSize = 0;
int filename_len;

void Connect(int socknum, struct sockaddr_in &client) {
    int client_len = sizeof(client);
    segment buf, buf2;
    memset(&buf, 0, sizeof(segment));

    // Accept first connection request. Make sure that it's a SYN and that it has no bit errors.
    while (Recvfrom(socknum, &buf, PKT_SIZE, 0, (struct sockaddr *) &client, (socklen_t *)&client_len) == false || buf.connection != SYN) {
		expected_ack = buf.seq;
		printReceive(expected_ack);
    }
    if (buf.connection == SYN) {
		expected_ack = buf.seq;
		Wrap_inc(expected_ack, 1);
        printReceive(expected_ack);
        buf2.ack = buf.seq + 1; // client sent 0 so we send ack 1
        buf2.seq = serv_seq;
        buf2.connection = SYN; //send back SYN request
        buf2.length = HEADER_SIZE;
        buf2.fileSize = fileSize;
    }
    Wrap_inc(serv_seq, 1);

    // Send initial response (ACK + server's SYN)
    SendtoC(socknum, &buf2, buf2.length, 0, (struct sockaddr *) &client, sizeof(client), 0, cwnd);

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

    // Receive the file request
    while (1) {
        rc = select(max_sd + 1, &working_set, NULL, NULL, &tv); //determine if socknum has any data to read
        if (rc == -1) {
            fprintf(stderr, "Select function failed. Error value is: %s\n", strerror(errno));
            exit(1);
        }

        if (duration >= RTO) { //resend packet after a time out
            SendtoC(socknum, &buf2, buf2.length, 0, (struct sockaddr *) &client, sizeof(client), 1, cwnd);
            start = clock(); //reset clock
        }

        if (rc) { //read from socket if data is available
            bool valid;
            valid = Recvfrom(socknum, &buf, PKT_SIZE, 0, (struct sockaddr *) &client, (socklen_t *)&client_len);
            // Check that the packet is the right one: the client's file request.
            if (valid && buf.ack == serv_seq) {  // We got the file request
				Wrap_inc(expected_ack, buf.length - HEADER_SIZE);  // increment by filename_len
                printReceive(expected_ack);
                break;
            } else {
                printReceive(expected_ack);
            }
        }

        duration = (clock() - start) / clk_per_ms; //increase duration to elapsed milliseconds
        FD_ZERO(&working_set);
        FD_SET(socknum, &working_set); //must re-include socknum in the working set of file descriptors
    }

    /* Extract requested file's name */
    filename_len = strlen(buf.msg);
    file = (char *) malloc(filename_len + 1);
    strcpy(file, buf.msg);
    file[filename_len] = '\0';
}

/** Returns the true case-sensitive filename corresponding to the requested
 * filename, or null if the file is not found. Example: client requests file.txt,
 * and this function returns FiLe.txt if that is the real name of the file in the
 * directory. */
char* get_true_filename(char* requestedFile) {
    /* Open the current directory. */
    DIR *dir_ptr;
    struct dirent *dirent_ptr;
    if ((dir_ptr = opendir(".")) == NULL) {
        fprintf(stderr, "System call opendir failed. Error value is %s\n", strerror(errno));
        exit(1);
    }

    /* Iterate through each directory entry in the current directory. */
    while ((dirent_ptr = readdir(dir_ptr)) != NULL) {
        /* Convert directory entry to lower case. The requested filename is
         *     already in lower case after parse_request() is called, so it does not
         *         require conversion. */
        char dirfile[strlen(dirent_ptr->d_name + 1)];
        int i;
        for (i = 0; dirent_ptr->d_name[i] != '\0'; i++) {
            dirfile[i] = tolower(dirent_ptr->d_name[i]);
        }
        dirfile[i] = '\0';

        /* Compare directory entry to requested filename */
        if (strcmp(dirfile, requestedFile) == 0) {
            /* If file is found, return its true name (case-sensitive) */
            char *trueName = (char*) malloc(strlen(dirent_ptr->d_name + 1));
            strcpy(trueName, dirent_ptr->d_name);
            closedir(dir_ptr);
            return trueName;
        }
    }
    /* Close the directory. */
    closedir(dir_ptr);
    return NULL;
}

/** Returns the lower case version of string. */
char* getLower(char *string, int len) {
    for (int i = 0; i < len; i++) {
        string[i] = tolower(string[i]);
    }
    return string;
}

void CloseConnection(int socknum, struct sockaddr_in &client) {
    // Send server's FIN packet
    int client_len = sizeof(client);
    segment pkt, rcvPkt;
    pkt.seq = serv_seq;
    pkt.connection = FIN;
    pkt.length = HEADER_SIZE;
    pkt.fileSize = fileSize;
    SendtoC(socknum, &pkt, pkt.length, 0, (struct sockaddr *) &client, sizeof(client), 0, cwnd);

    clock_t start, time_wait_start; //timer
    double duration = 0, time_wait_duration = 0; //elapsed time begins at zero
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
    bool valid;

    int count, max;
    max = 4*RTO; //just so we dont loop forever
    count = 0;

    // Check for client's ACK for server's FIN
    while (1) {
        rc = select(max_sd + 1, &working_set, NULL, NULL, &tv); //determine if socknum has any data to read
        if (rc == -1) {
            fprintf(stderr, "Select function failed. Error value is: %s\n", strerror(errno));
            exit(1);
        }

        if (duration >= RTO) { //resend packet after a time out
            count += RTO;
             if (count >= max) {  // If FIN never arrives at client, exit
                exit(0);
             }
            SendtoC(socknum, &pkt, pkt.length, 0, (struct sockaddr *) &client, sizeof(client), 1, cwnd);
            start = clock(); //reset clock
        }

        if (rc) { //read from socket if data is available
            bool valid;
            valid = Recvfrom(socknum, &rcvPkt, PKT_SIZE, 0, (struct sockaddr *) &client, (socklen_t *)&client_len);
            if (valid && rcvPkt.connection == FIN) {
                printReceive(expected_ack + 1);
                break;
            } else {
                printReceive(expected_ack);
            }

        }

        duration = (clock() - start) / clk_per_ms; //increase duration to elapsed milliseconds
        FD_ZERO(&working_set);
        FD_SET(socknum, &working_set); //must re-include socknum in the working set of file descriptors
    }

    // Send final ACK, for client's FIN
    Wrap_inc(expected_ack, 1);  // ACKing client's FIN, so +1
    Wrap_inc(serv_seq, 1);      // Final ACK's seq # is server's FIN + 1
    pkt.seq = serv_seq;
    pkt.ack = expected_ack;
    pkt.connection = 0;
    pkt.length = HEADER_SIZE;
    pkt.fileSize = fileSize;
    SendtoC(socknum, &pkt, pkt.length, 0, (struct sockaddr *) &client, sizeof(client), 0, cwnd);

    start = clock(); //note current time
    time_wait_start = clock();

    bool shouldSend = true;
    while (1) {
        rc = select(max_sd + 1, &working_set, NULL, NULL, &tv); //determine if socknum has any data to read
        if (rc == -1) {
            fprintf(stderr, "Select function failed. Error value is: %s\n", strerror(errno));
            exit(1);
        }

        if (duration >= RTO && shouldSend) { //resend packet after a time out
            //SendtoC(socknum, &pkt, pkt.length, 0, (struct sockaddr *) &client, sizeof(client), 1, cwnd);
            start = clock(); //reset clock
        }

         if (time_wait_duration >= (2*RTO)/1000)
             break;

        if (rc) { //read from socket if data is available
            valid = Recvfrom(socknum, &rcvPkt, PKT_SIZE, 0, (struct sockaddr *) &client, (socklen_t *)&client_len);
            printReceive(expected_ack);
            if (valid && rcvPkt.connection == FIN)
                break;
            else if (rcvPkt.ack == serv_seq)
                shouldSend = false;
        }

        duration = (clock() - start) / clk_per_ms; //increase duration to elapsed milliseconds
        time_wait_duration = ((clock() - time_wait_start) / (double)CLOCKS_PER_SEC);
        FD_ZERO(&working_set);
        FD_SET(socknum, &working_set); //must re-include socknum in the working set of file descriptors
    }
}

/** Sends the requested file to the client. */
void sendFile(char *file, int socknum, struct sockaddr_in &client) {
    /* Open the requested file in the directory. */
    int fd;
    if ((fd = open(file, O_RDONLY)) == -1){
        fprintf(stderr, "System call open failed. Error value is %s\n", strerror(errno));
        exit(1);
    }

    /* Find the size of the requested file. */
    struct stat file_stats;
    fstat(fd, &file_stats);
    fileSize = file_stats.st_size;

    /* Initialize timer. */
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
    int client_len = sizeof(client);


    /* Read file data and just send the first 5 packets for now. */
    queue<segment> packets;  // sliding window
    segment pkt, rcvPkt;
    int count = cwnd;
    bool shouldRestartClock = true;
    bool shouldRead = true;
    bool valid;
    int r;
	int win = 0;
    while (1){
        if (count > 0) {
            /* Read data chunk into pkt, send pkt, then push packet into queue (window). */
            if (shouldRead)
                r = Read(fd, pkt.msg, MSG_SIZE);
            if (r == 0 && packets.empty())
                break;
            else if (r == 0)
                shouldRead = false;
            else if (r != 0) {
                pkt.seq = serv_seq;
                pkt.length = r + HEADER_SIZE;
                pkt.ack = expected_ack;
                pkt.fileSize = fileSize;
                SendtoC(socknum, &pkt, pkt.length, 0, (struct sockaddr *) &client, sizeof(client), 0, cwnd);
                packets.push(pkt);

                /* Restart clock if needed. Set next expected_ack.  */
                if (shouldRestartClock) {
                    start = clock(); //note current time
                    shouldRestartClock = false;
                }
                Wrap_inc(serv_seq, r);
				win += r;
                count--;
            }
        }

        /* Select: See if a packet has arrived at the socket. */
        rc = Select(max_sd + 1, &working_set, NULL, NULL, &tv); //determine if socknum has any data to read

        /* If timer runs out, resend base packet and reset clock. */
        if (duration >= RTO) {
            SendtoC(socknum, &packets.front(), (&packets.front())->length, 0, (struct sockaddr *) &client, sizeof(client), 1, cwnd);
            start = clock();
        }

        // Read from socket if data is available
        if (rc) { 
            memset(&rcvPkt, 0, sizeof(segment));
            valid = Recvfrom(socknum, &rcvPkt, PKT_SIZE, 0, (struct sockaddr *) &client, (socklen_t *)&client_len);
            printReceive(expected_ack);  // this should always be constant anyway, until we start closing connection
            int diff = MAX_SEQ - packets.front().seq;
            int end_win = win - diff; //used to calculate if acks could wrap around max seq value
            if (valid && !packets.empty() && ((rcvPkt.ack > packets.front().seq && rcvPkt.ack <= packets.front().seq + win) || ((end_win > 0) && (rcvPkt.ack < end_win)))){ //expected_ack
                /* Slide the window down: dequeue all the packets that were ACK'd by the cumulative ACK. */
                while ((packets.front().seq < rcvPkt.ack && rcvPkt.ack <= packets.front().seq + win) || ((end_win > 0) && (rcvPkt.ack < end_win))) { //expected_ack
					win -= (packets.front().length - HEADER_SIZE);
					packets.pop();
                    count++;
                    shouldRestartClock = true;
                    diff = MAX_SEQ - packets.front().seq;
                    end_win = win - diff; //used to calculate if acks could wrap around max seq value

					if (packets.empty())
						break;
                }
            }
        }

        duration = (clock() - start) / clk_per_ms; //increase duration to elapsed milliseconds
        FD_ZERO(&working_set);
        FD_SET(socknum, &working_set); //must re-include socknum in the working set of file descriptors
    }
    r = close(fd);
    if (r == -1) {
        fprintf(stderr, "System call close failed. Error value is %s\n", strerror(errno));
        exit(1);
    }
}


int main(int argc, char **argv){
    /* Assert correct command line args. */
    if (argc != 2) {
        fprintf(stderr, "Incorrect command line args.\n"
                "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    /* Initialize the UDP socket used to communicate with client. */
    int socknum = socket(AF_INET, SOCK_DGRAM, 0);
    if (socknum == -1){
        fprintf(stderr, "System call socket failed. Error value is: %s\n", strerror(errno));
        exit(1);
    }

    /* Bind socket with server address and port number. */
    int portnum = atoi(argv[1]);
    struct sockaddr_in server, client;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_family = AF_INET;
    server.sin_port = htons(portnum);
    if (bind(socknum,(struct sockaddr *)&server, sizeof(server)) == -1){
        fprintf(stderr, "System call bind failed. Error value is: %s\n", strerror(errno));
        exit(1);
    }

	//initial congestion window size
	cwnd = 5;
	ssthresh = 15;

	srand(time(0));
	serv_seq = rand() % 30001;

    /* Wait for client's TCP connection request. */
    Connect(socknum, client);

    /* Find requested file in local directory. */
    file = getLower(file, filename_len);
    char *trueFile = get_true_filename(file);
    if (trueFile == NULL) {
        fprintf(stderr, "File requested by client not found!\n");
        exit(1);
    }   
    /* Send requested file to server. */
    sendFile(trueFile, socknum, client);

    /* Close connection to client. */
    CloseConnection(socknum, client);

    /* Free all dynamically allocated memory. */
    if (file)
        free(file);
    if (trueFile)
        free(trueFile);
}
