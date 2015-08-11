#include "qarb.h"
#include <string.h>
#include <math.h>


QueueingAdditiveRingBuffer::QueueingAdditiveRingBuffer( const unsigned int len ) {
	this->data = new float[len];
	this->datalen = len;
	this->idx = 0;
	this->last_idx = 0;
}

QueueingAdditiveRingBuffer::~QueueingAdditiveRingBuffer() {
	delete[] this->data;
}

void QueueingAdditiveRingBuffer::read(const unsigned int num_samples, float * outputBuff) {
	// If we have to wraparound to service this request, then do so by invoking ourselves twice
    if( this->idx + num_samples > this->datalen ) {
        unsigned int first_batch = this->datalen - this->idx;
        this->read(first_batch, outputBuff);
        this->read(num_samples - first_batch, outputBuff + first_batch);
    } else {
    	// Move idx so other people don't muck with our data
    	int tmpidx = this->idx;
    	this->idx = (this->idx + num_samples)%this->datalen;

    	// Ready data into outputBuff
        memcpy(outputBuff, this->data + tmpidx, num_samples*sizeof(float));

        // Zero out the stuff we just read in!
        memset(this->data + tmpidx, 0, num_samples*sizeof(float));
    }
}

void QueueingAdditiveRingBuffer::write(const unsigned int num_samples, const unsigned int offset, const float * inputBuff) {
	unsigned int temp_idx = (this->idx + offset)%this->datalen;
	unsigned int amnt_written = 0;
	while( amnt_written < num_samples ) {
		// Instead of memcpy'ing like an ordinary ringbuffer, we ADD, and we don't update this->idx!
		unsigned int batch_size = fmin(this->datalen - temp_idx, num_samples - amnt_written);
		for( unsigned int i=0; i<batch_size; ++i)
			this->data[temp_idx + i] += inputBuff[i + amnt_written];

		amnt_written += batch_size;
		temp_idx = (temp_idx + batch_size)%this->datalen;
	}
}

// Return the amount read from the ringbuffer since the last time we asked about it!
unsigned int QueueingAdditiveRingBuffer::getDelta() {
    int amnt = this->last_idx - this->idx;
    if( amnt < 0 )
        amnt += this->datalen;
    this->last_idx = this->idx;
    return amnt;
}