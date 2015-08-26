#include <map>
#include <string>

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