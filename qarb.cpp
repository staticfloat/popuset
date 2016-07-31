#include "qarb.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

QueueingAdditiveRingBuffer::QueueingAdditiveRingBuffer( const unsigned int len ) {
	this->data = new float[len];
    memset(this->data, 0, sizeof(float)*len);
	this->datalen = len;
	this->idx = 0;
	this->last_idx = 0;
    this->farthest_write_idx = 0;
}

QueueingAdditiveRingBuffer::~QueueingAdditiveRingBuffer() {
	delete[] this->data;
}

unsigned int QueueingAdditiveRingBuffer::circularDistance( unsigned int start, unsigned int end) {
    int amnt = end - start;
    if( amnt < 0 )
        amnt += this->datalen;
    return amnt;
}

void QueueingAdditiveRingBuffer::read(const unsigned int num_samples, float * outputBuff) {
    //printf("QARB: Reading %d\n", num_samples);
	// If we have to wraparound to service this request, then do so by invoking ourselves twice
    if( this->idx + num_samples > this->datalen ) {
        unsigned int first_batch = this->datalen - this->idx;
        this->read(first_batch, outputBuff);
        this->read(num_samples - first_batch, outputBuff + first_batch);
    } else {
        // Move idxs so other people don't muck with our data
        int old_idx = this->idx;
    	this->idx = (this->idx + num_samples)%this->datalen;

        // Update write_idxs to not fall behind this->idx
        for( auto &kv : this->write_idxs ) {
            if( circularDistance(old_idx, kv.second) < num_samples )
                this->write_idxs[kv.first] = this->idx;
        }

        if( circularDistance(old_idx, this->farthest_write_idx) < num_samples ) {
            this->farthest_write_idx = this->idx;
        }

    	// Ready data into outputBuff
        memcpy(outputBuff, this->data + old_idx, num_samples*sizeof(float));

        // Zero out the stuff we just read in!
        memset(this->data + old_idx, 0, num_samples*sizeof(float));
    }
}

void QueueingAdditiveRingBuffer::write(const unsigned int num_samples, const std::string client_ident, const float * inputBuff) {
    // If we've never seen this client before, then initialize his index
    if( this->write_idxs.find(client_ident) == this->write_idxs.end() )
        this->write_idxs[client_ident] = this->idx;

    unsigned int write_idx = this->write_idxs[client_ident];
	unsigned int amnt_written = 0;
	while( amnt_written < num_samples ) {
		// Instead of memcpy'ing like an ordinary ringbuffer, we ADD, and we don't update this->idx!
		unsigned int batch_size = fmin(this->datalen - write_idx, num_samples - amnt_written);
		for( unsigned int i=0; i<batch_size; ++i)
			this->data[write_idx + i] += inputBuff[i + amnt_written];

		amnt_written += batch_size;
		write_idx = (write_idx + batch_size)%this->datalen;
	}

    // Update the write_idx
    this->write_idxs[client_ident] = write_idx;

    // Update farthest_write_idx
    unsigned int max_dist = fmax(circularDistance(this->idx, write_idx), circularDistance(this->idx, this->farthest_write_idx));
    this->farthest_write_idx = (this->idx + max_dist)%this->datalen;
}

unsigned int QueueingAdditiveRingBuffer::getMaxReadable() {
    return circularDistance(this->idx, this->farthest_write_idx);
}

void QueueingAdditiveRingBuffer::clearClient( std::string client_ident ) {
    this->write_idxs.erase(client_ident);
}

// You probably don't need to call this.
unsigned int QueueingAdditiveRingBuffer::getIdx() {
    return this->idx;
}