CC=g++
CFLAGS+=-I$(shell echo ~)/local/include -std=c++11
LDFLAGS+=-L$(shell echo ~)/local/lib -lportaudio -lopus -lzmq
SRC=popuset.cpp audio.cpp qarb.cpp util.cpp wavfile.cpp
HEADERS=popuset.h audio.h qarb.h util.h wavfile.h

all: release debug

popuset: $(SRC) $(HEADERS) Makefile
	$(CC) $(CFLAGS) -O3 -o popuset $(SRC) $(LDFLAGS)

popuset-debug: $(SRC) $(HEADERS) Makefile
	$(CC) $(CFLAGS) -g -O0 -o popuset-debug $(SRC) $(LDFLAGS)

release: popuset
debug: popuset-debug

clean:
	rm popuset popuset-debug
