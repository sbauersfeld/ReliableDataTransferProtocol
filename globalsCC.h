#ifndef GLOBALS
#define GLOBALS

#include <iostream>
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
using namespace std;

#define PKT_SIZE 1024
#define MSG_SIZE 1004
#define HEADER_SIZE 20
#define SYN 1
#define FIN 2
unsigned int MAX_SEQ = 4294967295;//30720
int MAX_CWND = 1000000;//2147483647;//15360
#define RTO 500

struct segment {
	int length;
	unsigned int seq;
	unsigned int ack;
	int fileSize;
	int connection;
	char msg[MSG_SIZE];
};

void printMsg(char *msg, int len) {
	cerr << "Printing msg:\n";
	for (int i = 0; i < len; i++) {
		cerr << msg[i];
	}
	cerr << endl;
}

void Wrap_inc(unsigned int &value, unsigned int amt) {
	int64_t v1 = value;
	int64_t v2 = amt;
	v1 = v1 + v2;
	v2 = 0xFFFFFFFF;
	v2 += 1;
	v1 = v1 % v2;
	value = v1;
}

unsigned int ComputeChecksum(const segment seg, int size) {
	unsigned int sum = 0;
	sum = seg.length + seg.seq + seg.ack + seg.connection; //+checksum?
	for (int i = 0; i < size; i++) {
		sum += (unsigned int) (seg.msg[i] & 0xFF); //sum up all the bytes
	}
	return sum;
}

/** Copies a char buffer's contents into another char buffer (for non-NULL-terminated buffers. */
void copyBuffer(char *dest, const char *src, int len) {
	// Assumes that dest is already allocated more bytes than the length of src.
	for (int i = 0; i < len; i++) {
		dest[i] = src[i];
	}
}

uint32_t ComputeSum(const segment seg, int len) {
	char *data;
	int newLen;
	// If data length is odd, it's not divisible by 2 bytes == 16 bits, so pad it with a 0 byte.
	if (len % 2 == 1) {
		newLen = len + 1;
		data = (char*) malloc(newLen);
		copyBuffer(data, seg.msg, len);
		data[len] = 0;
	} else {
		newLen = len;
		data = (char*) malloc(newLen);
		copyBuffer(data, seg.msg, len);
	}

	// Combine pairs of data bytes into 16-bit words. Find the sum of all 16-bit words.
	uint16_t _16bitWord;
	uint32_t sum = 0;
	for (int i = 0; i < newLen; i += 2) {
		_16bitWord = data[i];
		_16bitWord = _16bitWord << 8;
		_16bitWord += data[i + 1];
		sum += _16bitWord;
	}

	// Add header fields into the sum.
	sum += seg.length + seg.seq + seg.ack + seg.connection;

	if (data)
		free(data);
	return sum;
}

uint16_t ComputeStrongChecksum(const segment seg, int len) {
	uint32_t sum = ComputeSum(seg, len);

	// If the lower 16 bits of sum overflowed, wrap around and add the carries.
	// If the act of adding the carries overflows the lower 16 bits of sum again,
	// keep wrapping the carries around until the higher 16 bits of sum are 0.
	uint32_t sumLower16;
	while ((sum >> 16) > 0) {
		sumLower16 = sum & 0xFFFF;
		sum = (sum >> 16) + sumLower16;
	}

	// Return the one's complement of the sum as a 16-bit checksum.
	sum = ~sum;
	return (uint16_t) sum;
}

/** Returns true if no error detected, otherwise false. */
bool VerifyChecksum(const segment seg, int len) {
	uint32_t sum = ComputeSum(seg, len);

	// DEBUG:
	//fprintf(stderr, "Checksum I received: %d\n", seg.checksum);

	//sum += seg.checksum;
	// If the lower 16 bits of sum overflowed, wrap around and add the carries.
	// If the act of adding the carries overflows the lower 16 bits of sum again,
	// keep wrapping the carries around until the higher 16 bits of sum are 0.
	uint32_t sumLower16;
	while ((sum >> 16) > 0) {
		sumLower16 = sum & 0xFFFF;
		sum = (sum >> 16) + sumLower16;
	}


	// If all bits in sum are 1 (aka if the complement of sum is 0), then there is no error.
	if ((~sum & 0xFFFF) == 0) {
		return true;
	}
	return false;
}

void printReceive(unsigned int acknum) {
	fprintf(stdout, "Receiving packet %u\n", acknum);
}

bool Recvfrom(int socknum, segment *buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen) {
	int ret;
	ret = recvfrom(socknum, (void*) buf, len, flags, src_addr, addrlen);
	if (ret < 0) {
		fprintf(stderr, "System call recvfrom failed. Error value is: %s\n", strerror(errno));
		exit(1);
	}
	 return true;
	 /*unsigned int sum = ComputeChecksum(*buf, ret - HEADER_SIZE);
	 if (sum == buf->checksum) { //compare sums
	 	//cerr << "No error detected\n";
	 	return true;
	 }
	 else {
	 	fprintf(stderr, "Corruption on packet with seq# %d!\n", buf->seq);  // DEBUG
	 	cerr << "Packet contains checksum " << buf->checksum << endl;
	 	cerr << "ComputeChecksum returns " << sum << endl;
	 	cerr << "ComputeStrongChecksum returns " << ComputeStrongChecksum(*buf, ret - HEADER_SIZE) << endl;
	 	cerr << "ComputeSum returns " << ComputeSum(*buf, ret - HEADER_SIZE) << endl;
	 	return false;
	 }*/
}

void SendtoC(int socknum, segment * buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t dest_len, bool R, int cwnd, int ssthresh) {
	int ret;
	//unsigned int sum = ComputeChecksum(*buf, len - HEADER_SIZE); //calculate checksum
	//buf->checksum = sum;
	ret = sendto(socknum, (void*) buf, len, flags, dest_addr, dest_len);
	if (ret < 0) {
		fprintf(stderr, "System call sendto() failed. Error value is: %s\n", strerror(errno));
		exit(1);
	}
	if (R && buf->connection == SYN) {
		fprintf(stdout, "Sending packet %u %d %d Retransmission SYN\n", buf->seq, cwnd*PKT_SIZE, ssthresh*PKT_SIZE);
	}
	else if (R && buf->connection == FIN) {
		fprintf(stdout, "Sending packet %u %d %d Retransmission FIN\n", buf->seq, cwnd*PKT_SIZE, ssthresh*PKT_SIZE);
	}
	else if (R) {
		fprintf(stdout, "Sending packet %u %d %d Retransmission\n", buf->seq, cwnd*PKT_SIZE, ssthresh*PKT_SIZE);
	}
	else if (buf->connection == SYN) {
		fprintf(stdout, "Sending packet %u %d %d SYN\n", buf->seq, cwnd*PKT_SIZE, ssthresh*PKT_SIZE);
	}
	else if (buf->connection == FIN) {
		fprintf(stdout, "Sending packet %u %d %d FIN\n", buf->seq, cwnd*PKT_SIZE, ssthresh*PKT_SIZE);
	}
	else {
		fprintf(stdout, "Sending packet %u %d %d\n", buf->seq, cwnd*PKT_SIZE, ssthresh*PKT_SIZE);
	}
}

void SendtoS(int socknum, segment * buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t dest_len, bool R) {
	int ret;
	// unsigned int sum = ComputeChecksum(*buf, len - HEADER_SIZE); //calculate checksum
	// buf->checksum = sum;
	ret = sendto(socknum, (void*)buf, len, flags, dest_addr, dest_len);
	if (ret < 0) {
		fprintf(stderr, "System call sendto() failed. Error value is: %s\n", strerror(errno));
		exit(1);
	}
	if (R && buf->connection == SYN) {
		fprintf(stdout, "Sending packet %u Retransmission SYN\n", buf->seq);
	}
	else if (R && buf->connection == FIN) {
		fprintf(stdout, "Sending packet %u Retransmission FIN\n", buf->seq);
	}
	else if (R) {
		fprintf(stdout, "Sending packet %u Retransmission\n", buf->seq);
	}
	else if (buf->connection == SYN) {
		fprintf(stdout, "Sending packet %u SYN\n", buf->seq);
	}
	else if (buf->connection == FIN) {
		fprintf(stdout, "Sending packet %u FIN\n", buf->seq);
	}
	else {
		fprintf(stdout, "Sending packet %u\n", buf->seq);
	}
}

ssize_t Read(int fd, void *buf, size_t count) {
    int r = read(fd, buf, count);
    if (r < 0) {
        fprintf(stderr, "System call read() failed. Error value is %s\n", strerror(errno));
        exit(1);
    }
    return r;
}

int Select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    int r = select(nfds, readfds, writefds, exceptfds, timeout);
    if (r == -1) {
        fprintf(stderr, "select() function failed. Error value is: %s\n", strerror(errno));
        exit(1);
    }
    return r;
}

#endif //GLOBALS
