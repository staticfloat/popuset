all: build/receiver

SOURCES = $(foreach S,$(wildcard receiver/*.c),$(notdir $(S)))
OBJECTS = $(patsubst %.c,build/%.o,$(SOURCES))
CFLAGS = -O2 -g
LDFLAGS = -lasound -lopus -lm -lpthread

build:
	mkdir -p $@

build/%.o: receiver/%.c receiver/receiver.h | build
	$(CC) -c  -o $@ $(CFLAGS) $<

build/receiver: $(OBJECTS)
	$(CC) -o $@ $(LDFLAGS) $^

run: build/receiver
	sudo build/receiver

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