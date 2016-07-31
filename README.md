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

`popuset` implements a fully connectable audio mesh network.  Each instance of `popuset` can be instructed to open a single device (for input, output, or both) and listens for incoming connections (on port `5040` by default, settable with the `--port/-p` option).  `popuset` instances are targeted at eachother using the `--target/-t` option.  To send audio from computer A's microphone to computer B's speakers, you would therefore instruct computer B to open its output device (using the `--device/-d` option). You would then tell computer A to open the microphone audio device (via the `--device/-d` option) and target it at computer B with the `--target/-t` option.  Note that multiple `popuset` instances can target the same output instance, and they will all be mixed together in realtime.

You can "log" input/output audio to `.wav` files using the `--log/-l` option.  This is useful for debugging, or just recording audio that other users are sending your way.


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
