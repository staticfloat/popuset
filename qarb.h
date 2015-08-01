class QueueingAdditiveRingBuffer {
public:
	QueueingAdditiveRingBuffer( const unsigned int len );
	~QueueingAdditiveRingBuffer();

	void read(const unsigned int num_samples, float * outputBuff);
	void write(const unsigned int num_samples, const unsigned int offset, const float * inputBuff);

	unsigned int getDelta();
protected:
	float * data;
	unsigned int len, idx, last_idx;
}