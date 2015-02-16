# popuset
portaudio opus network streamer

Build instructions
==================
Make sure you have `libportaudio`, `libzmq` and `libopus` installed.  For example, on OSX, run:
```
$ brew install portaudio zmq opus
```

Then, checkout the sources and run `make` inside the source directory.


Usage
=====

Usage is pretty simple.  run `./popuset --help` to see an overview of options, along with example command line usages.  Start one server and one client, (clients have the `-r` option specified) and enjoy your audio!
