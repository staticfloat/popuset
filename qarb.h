#include <map>
#include <string>

/*
The Queueing Additive Ring Buffer (QARB, pronounced "Carb") is a datastructure
used to simplify the mixing together of multiple client audio streams into a
single resultant audio stream in realtime.  This is done by having a single
underlying ring buffer that is added into by multiple clients, all of which
track their own respective location within the ring buffer.

The underlying ring buffer is read from at a particular rate; clients that
overrun this rate and the amount of available buffer space have their excess
output discarded, clients that underrun this rate create discontinuities in
their output.
*/
class QueueingAdditiveRingBuffer {
public:
	QueueingAdditiveRingBuffer( const unsigned int len );
	~QueueingAdditiveRingBuffer();

	void read(const unsigned int num_samples, float * outputBuff);
	void write(const unsigned int num_samples, const std::string client_ident, const float * inputBuff);

	unsigned int getMaxReadable();
	unsigned int getIdx();

	void clearClient( const std::string client_ident );
protected:
	unsigned int circularDistance( unsigned int start, unsigned int end);
	float * data;
	unsigned int datalen, idx, last_idx, farthest_write_idx;

	std::map<std::string, unsigned int> write_idxs;
};
