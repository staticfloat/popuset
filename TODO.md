TODO
====

* Add proper logging system
* Proper exceptions
* Auto-snag default output device on OSX
* mDNS discovery
* Multiple client mixing
* Multiple soundcard capture
* Multichannel capture?
* Encryption?
* Fade audio in when a client (re-)connects



What I need to do next:
- Change offset calculations; they're brittle and annoying.
- Specifically, this->idx can change (without updating offsets) and cause all sorts of problems with clients