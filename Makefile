CC=g++
CFLAGS=-O2 -march=native -mtune=native
LDFLAGS=-lportaudio -lopus -lzmq

all:
	$(CC) $(CFLAGS) -o popuset *.cpp $(LDFLAGS)

clean:
	rm popuset
