class RingBuffer {
public:
	RingBuffer( const unsigned int len );
	~RingBuffer();

	// Return the number of samples readable or writable
	unsigned int writable();
	unsigned int readable();

	// Only return true if readIndex is at least num_samples ahead of writeIndex
	bool writable( const unsigned int num_samples );
	// Only return true if writeIndex is at least num_samples ahead of readIndex
	bool readable( const unsigned int num_samples );

	bool read( const unsigned int num_samples, float * outputBuff);
	bool write( const unsigned int num_samples, const float * inputBuff);

	// Return the amount written into the ringbuffer since the last time we asked about it!
	int getAmountWritten();

	// Return the amount read from the ringbuffer since the last time we asked about it!
	int getAmountRead();
protected:
	float * data;
	unsigned int readIdx, writeIdx, len;
	unsigned int last_writeIdx, last_readIdx;
};
