#include "../qarb.h"
#include "../wavfile.h"
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <math.h>

bool should_quit = false;
bool should_produce = true;
bool should_consume = true;
#define QARB_LEN 1000
#define WRITE_CHUNK 10
#define READ_CHUNK 10
#define WRITE_MS 50
#define READ_MS (READ_CHUNK*WRITE_MS/WRITE_CHUNK)

void * consumer(void * data) {
	QueueingAdditiveRingBuffer * qarb = (QueueingAdditiveRingBuffer *)data;
	WAVFile * wav = new WAVFile("qarb_output.wav", 1, 1000);

	float buff[READ_CHUNK];
	// Just pull data out of this guy until we should quit
	while( !should_quit ) {
		usleep(READ_MS*1000);

		if( should_consume ) {
			unsigned int readable = qarb->getMaxReadable();
			if( readable < READ_CHUNK )
				printf("UNDERRUN (%d)\n", readable);
			else {
				qarb->read(READ_CHUNK, &buff[0]);
				wav->writeData(&buff[0], READ_CHUNK);
			}
		}
	}

	delete wav;
	return NULL;
}

void * producer1(void * data) {
	QueueingAdditiveRingBuffer * qarb = (QueueingAdditiveRingBuffer *)data;

	float buff[WRITE_CHUNK];
	// Push data into this guy until we should quit
	int count = -100;
	int offset = 0;
	std::string ident = "producer1";

	while( !should_quit ) {
		usleep(WRITE_MS*1000);

		if( should_produce ) {
			for( int i=0; i<WRITE_CHUNK; ++i )
				buff[i] = count;

			qarb->write(WRITE_CHUNK, ident, buff);
			offset += WRITE_CHUNK;
			count++;
			if( count > 100 )
				count = -100;
		}
	}
	return NULL;
}

void * monitor(void * data) {
	QueueingAdditiveRingBuffer * qarb = (QueueingAdditiveRingBuffer *)data;

	while( !should_quit ) {
		unsigned int start_idx = qarb->getIdx()/20;
		unsigned int end_idx = ((qarb->getIdx() + qarb->getMaxReadable())%QARB_LEN)/20;

		printf("\r[");
		if( end_idx < start_idx ) {
			for( int i=0; i<end_idx; ++i )
				printf("*");
			for( int i=end_idx; i<start_idx; ++i )
				printf(" ");
			for( int i=start_idx; i<50; ++i )
				printf("*");
		} else {
			for( int i=0; i<start_idx; ++i )
				printf(" ");
			for( int i=start_idx; i<end_idx; ++i )
				printf("*");
			for( int i=end_idx; i<50; ++i )
				printf(" ");
		}
		
		printf("]");
		fflush(stdout);
		usleep(10*1000);
	}
	return NULL;
}

void sigint_handler(int dummy=0) {
    should_quit = true;
    // Undo our signal handling here, so mashing CTRL-C will definitely kill us
    signal(SIGINT, SIG_DFL);
    printf("Signal received, shutting down...\n");
}



int main( void ) {
	QueueingAdditiveRingBuffer * qarb = new QueueingAdditiveRingBuffer(QARB_LEN);

	// Start producer and consumer threads
	pthread_t producer1_thread;
	pthread_create(&producer1_thread, NULL, producer1, (void *)qarb);
	pthread_t consumer_thread;
	pthread_create(&consumer_thread, NULL, consumer, (void *)qarb);
	pthread_t monitor_thread;
	pthread_create(&monitor_thread, NULL, monitor, (void *)qarb);

	// Now sit in a loop until we get a SIGINT
	signal(SIGINT, sigint_handler);
	printf("\n");

	while( !should_quit ) {
		usleep(10000);
	}
	
	pthread_join(monitor_thread, NULL);
	pthread_join(producer1_thread, NULL);
	pthread_join(consumer_thread, NULL);

	delete qarb;
	printf("Done!\n");
}