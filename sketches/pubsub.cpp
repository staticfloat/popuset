#include <zmq.h>
#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>


void * slave(void * ctx) {
	void * sub = zmq_socket(ctx, ZMQ_SUB);
	zmq_connect(sub, "inproc://broker_cmd");
	zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);

	zmq_pollitem_t items[1] = {sub, 0, ZMQ_POLLIN, 0};
	while(true) {
		printf("[sub] polling...\n");
		zmq_poll(&items[0], 1, -1);
		if( items[0].revents & ZMQ_POLLIN ) {
			int blah;
			zmq_recv(sub, &blah, sizeof(int), 0);
			blah = htonl(blah);
			printf("[sub] Got 'em, coach! (%d)\n", blah);
		}
	}
}


int main(void) {
	void * ctx = zmq_ctx_new();

	void * pub = zmq_socket(ctx, ZMQ_PUB);
	if( pub == NULL ) {
		fprintf(stderr, "Failed to create pub: %s\n", strerror(errno));
	}
	int rc = zmq_bind(pub, "inproc://broker_cmd");
	if( rc != 0 ) {
		fprintf(stderr, "Failed to bind pub: %s\n", strerror(errno));
	}

	pthread_t thread;
	pthread_create(&thread, NULL, slave, ctx);

	int i=0;
	while(true) {
		int b = htonl(i);
		zmq_send(pub, "heh", 3, ZMQ_DONTWAIT | ZMQ_SNDMORE);
		zmq_send(pub, &b, sizeof(int), ZMQ_DONTWAIT);
		printf("[pub] Sending %d...\n", i);
		i++;

		sleep(1);
	}
}