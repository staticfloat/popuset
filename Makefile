CC=g++
CFLAGS+=-I$(shell echo ~)/local/include
LDFLAGS+=-L$(shell echo ~)/local/lib -lportaudio -lopus -lzmq

popuset: popuset.cpp
	$(CC) $(CFLAGS) -O3 -o popuset *.cpp $(LDFLAGS)

popuset-debug: popuset.cpp
	$(CC) $(CFLAGS) -g -o popuset-debug *.cpp $(LDFLAGS)

all: popuset
debug: popuset-debug

clean:
	rm popuset
