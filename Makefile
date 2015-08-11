CC=g++
CFLAGS+=-I$(shell echo ~)/local/include -std=c++11
LDFLAGS+=-L$(shell echo ~)/local/lib -lportaudio -lopus -lzmq
SRC=popuset.cpp audio.cpp qarb.cpp util.cpp

popuset: $(SRC) Makefile
	$(CC) $(CFLAGS) -O3 -o popuset $(SRC) $(LDFLAGS)

popuset-debug: $(SRC) Makefile
	$(CC) $(CFLAGS) -g -O0 -o popuset-debug $(SRC) $(LDFLAGS)

all: popuset
debug: popuset-debug

clean:
	rm popuset popuset-debug
