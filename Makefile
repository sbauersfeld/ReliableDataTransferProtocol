CC=g++
CFLAGS=-Wall -Wextra

all:
	$(CC) $(CFLAGS) server.cpp -o server
	$(CC) $(CFLAGS) client.cpp -o client
	$(CC) $(CFLAGS) serverCC.cpp -o serverCC
	$(CC) $(CFLAGS) clientCC.cpp -o clientCC
clean:
	rm -f server client serverCC clientCC project2.tar
dist:
	tar -czvf project2.tar client.cpp server.cpp clientCC.cpp serverCC.cpp globals.h globalsCC.h report.pdf Makefile README 
