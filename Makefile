CC=g++
CFLAGS=-O3
LDFLAGS=-lportaudio -lopus -lzmq

all:
	$(CC) $(CFLAGS) -o popuset *.cpp $(LDFLAGS)

clean:
	rm popuset
