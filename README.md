popuset
=======
portaudio opus network streamer

Build instructions
==================
Make sure you have `libportaudio`, `libzmq` and `libopus` installed.  For example, on OSX, run:
```
$ brew install portaudio zmq opus
```

On Debian-based systems run:
```
$ sudo apt-get install portaudio19-dev libzmq3-dev libopus-dev
```

Then, checkout the sources and run `make` inside the source directory.


Usage
=====

Usage is pretty simple.  run `./popuset --help` to see an overview of options, along with example command line usages.  Start one server and one client, (clients have the `-r` option specified) and enjoy your audio!


Raspi notes
===========

Since we need a newer version of `libzmq` than is given us by the distribution packages, we need to download/compile it ourselves.  First, download/install all necessary dependencies:
```
$ sudo apt-get install autoconf automake g++ gcc libtool portaudio19-dev libopus-dev
```

Next, download/compile/install `libzmq`.  Note that this took 25 minutes on my raspi.
```
$ git clone https://github.com/zeromq/libzmq.git
$ cd libzmq
$ ./autogen.sh
$ ./configure --without-libsodium --without-pgm
$ make
$ sudo make install
```

Finally, download/compile this puppy:
```
$ git clone https://github.com/staticfloat/popuset.git
$ cd popuset
$ make
```