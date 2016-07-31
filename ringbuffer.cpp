#include "ringbuffer.h"


RingBuffer::RingBuffer( const unsigned int len ) {
	this->data = new float[len];
    memset( this->data, 0, sizeof(float)*len );
	this->readIdx = 0;
	this->writeIdx = 0;
    this->last_writeIdx = 0;
    this->last_readIdx = 0;
	this->len = len;
}

RingBuffer::~RingBuffer() {
	delete[] this->data;
}

// Return the number of samples readable from this buffer
unsigned int RingBuffer::writable() {
    int readIdx_adjusted = readIdx;
    
    // Note that this is <= because if the two indexes are equal, we can write!
    if( readIdx_adjusted <= writeIdx )
        readIdx_adjusted += len;
    return readIdx_adjusted - writeIdx;
}

// Return the number of samples readable from this buffer
unsigned int RingBuffer::readable() {
    int writeIdx_adjusted = writeIdx;
    
    if( writeIdx_adjusted < readIdx )
        writeIdx_adjusted += len;
    return writeIdx_adjusted - readIdx;
}

// Only return true if readIndex is at least num_samples ahead of writeIndex
bool RingBuffer::writable( const unsigned int num_samples ) {
    return writable() > num_samples;
}

// Only return true if writeIndex is at least num_samples ahead of readIndex
bool RingBuffer::readable( const unsigned int num_samples ) {
    return readable() > num_samples;
}

bool RingBuffer::read( const unsigned int num_samples, float * outputBuff ) {
    if( !readable(num_samples) ) {
        //printf("[%d] Won't read %d from ringbuffer, (W: %d, R: %d)\n", packet_count, num_samples, writeIdx, readIdx);
        return false;
    }

    // If we have to wraparound to service this request, then do so by invoking ourselves twice
    if( readIdx + num_samples > len ) {
        unsigned int first_batch = len - readIdx;
        read(first_batch, outputBuff);
        read(num_samples - first_batch, outputBuff + first_batch);
    } else {
        memcpy(outputBuff, this->data + readIdx, num_samples*sizeof(float));
        memset(this->data + readIdx, 0, num_samples*sizeof(float));
        readIdx = (readIdx + num_samples)%len;
    }
    return true;
}

bool RingBuffer::write( const int num_samples, const float * inputBuff ) {
    if( !writable(num_samples) ) {
        //printf("[%d] Won't write %d to ringbuffer, (W: %d, R: %d)\n", packet_count, num_samples, writeIdx, readIdx);
        return false;
    }
    
    if( writeIdx + num_samples > len ) {
        unsigned int first_batch = len - writeIdx;
        write(first_batch, inputBuff);
        write(num_samples - first_batch, inputBuff + first_batch);
    } else {
        memcpy(this->data + writeIdx, inputBuff, num_samples*sizeof(float));
        writeIdx = (writeIdx + num_samples)%len;
    }
    return true;
}

int RingBuffer::getAmountWritten() {
    int amnt = this->last_writeIdx - this->writeIdx;
    if( amnt < 0 )
        amnt += this->len;
    this->last_writeIdx = this->writeIdx;
    return amnt;
}

int RingBuffer::getAmountRead() {
    int amnt = this->last_readIdx - this->readIdx;
    if( amnt < 0 )
        amnt += this->len;
    this->last_readIdx = this->readIdx;
    return amnt;
}