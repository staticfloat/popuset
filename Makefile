CC=g++
CFLAGS=-O3 -march=native -mtune=native
LDFLAGS=-lportaudio -lopus -lzmq

all:
	$(CC) $(CFLAGS) -o popuset *.cpp $(LDFLAGS)

clean:
	rm popuset
