CC=g++
CFLAGS=-O3
CFLAGS_debug=-g
LDFLAGS=-lportaudio -lopus -lzmq

all:
	$(CC) $(CFLAGS) -o popuset *.cpp $(LDFLAGS)

debug:
	$(CC) $(CFLAGS_debug) -o popuset *.cpp $(LDFLAGS)

clean:
	rm popuset
