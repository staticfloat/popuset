CC=g++
CFLAGS+=-I$(shell echo ~)/local/include
LDFLAGS+=-L$(shell echo ~)/local/lib -lportaudio -lopus -lzmq
SRC=popuset.cpp audio.cpp qarb.cpp util.cpp net.cpp

popuset: $(SRC)
	$(CC) $(CFLAGS) -O3 -o popuset $(SRC) $(LDFLAGS)

popuset-debug: popuset.cpp
	$(CC) $(CFLAGS) -g -o popuset-debug $(SRC) $(LDFLAGS)

all: popuset
debug: popuset-debug

clean:
	rm popuset
