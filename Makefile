all: build/receiver

SOURCES = $(foreach S,$(wildcard receiver/*.c),$(notdir $(S)))
OBJECTS = $(patsubst %.c,build/%.o,$(SOURCES))
CFLAGS = -O2 -g
LDFLAGS = -lportaudio -lopus -lm -lpthread

build:
	mkdir -p $@

build/%.o: receiver/%.c receiver/receiver.h | build
	$(CXX) -c  -o $@ $(CFLAGS) $<

build/receiver: $(OBJECTS)
	$(CXX) -o $@ $(LDFLAGS) $^

run: build/receiver
	build/receiver

gdb: build/receiver
	gdb --args build/receiver

deps:
	sudo apt install -y libasound2-dev libzmq-dev libopus-dev

push:
	make clean
	rsync -Pav . pi@speakerpi0:~/src/alsastream/

clean:
	rm -rf build

print-%:
	echo $*=$($*)