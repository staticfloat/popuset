#include <semaphore.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <zmq.h>
#include <string.h>

class ZMQSemaphore {
public:
	ZMQSemaphore(void * zmq_ctx);
	~ZMQSemaphore();

	void post();
	bool wait();
	ZMQSemaphore * spawnSibling();

private:
	ZMQSemaphore(void * zmq_ctx, const char * addr);
	void gen_random_addr();

	char * addr;
	void * zmq_ctx;
	void * sock;
};

ZMQSemaphore::ZMQSemaphore(void * zmq_ctx) {
	this->zmq_ctx = zmq_ctx;
	this->sock = zmq_socket(zmq_ctx, ZMQ_PAIR);
	int linger = 0;
    zmq_setsockopt(sock, ZMQ_LINGER, &linger, sizeof(int));

	this->gen_random_addr();
	printf("Binding to %s...\n", this->addr);
	if( zmq_bind(sock, this->addr) != 0 ) {
		this->gen_random_addr();
		printf("Failed, Binding to %s...\n", this->addr);
		if( zmq_bind(sock, this->addr) != 0 ) {
			throw "Could not bind zmq semaphore socket";
		}
	}
}

ZMQSemaphore::~ZMQSemaphore() {
	delete[] this->addr;
	char c = -1;
	zmq_send(this->sock, &c, 1, ZMQ_DONTWAIT);
	zmq_close(this->sock);
}

void ZMQSemaphore::post() {
	char c = 0;
	zmq_send(this->sock, &c, 1, ZMQ_DONTWAIT);
}

bool ZMQSemaphore::wait() {
	char c;
	if( zmq_recv(this->sock, &c, 1, 0) == -1 ) {
		printf("WAIT FAILED!\n");
		return false;
	}
	if( c == -1 ) {
		printf("WE DONE HERE\n");
		return false;
	}
	return true;
}

ZMQSemaphore::ZMQSemaphore(void * zmq_ctx, const char * addr) {
	this->zmq_ctx = zmq_ctx;
	this->addr = new char[strlen(addr)+1];
	strcpy(this->addr, addr);
	this->sock = zmq_socket(zmq_ctx, ZMQ_PAIR);
	int linger = 0;
    zmq_setsockopt(sock, ZMQ_LINGER, &linger, sizeof(int));

	printf("Connecting to %s...\n", this->addr);

	if( zmq_connect(sock, this->addr ) != 0 ) {
		printf("Could not connect zmq semaphore socket\n");
		throw "Could not connect zmq semaphore socket";
	}
}

void ZMQSemaphore::gen_random_addr() {
    static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    
    this->addr = new char[16];
    sprintf(this->addr, "inproc://");
    for (int i = 9; i < 15; ++i)
        this->addr[i] = alphanum[rand() % (sizeof(alphanum) - 1)];

    this->addr[15] = 0;
}

ZMQSemaphore * ZMQSemaphore::spawnSibling() {
	return new ZMQSemaphore(this->zmq_ctx, this->addr);
}


void * thread(void * data) {
	ZMQSemaphore * sem = ((ZMQSemaphore *)data)->spawnSibling();

	printf("Waiting (%p)...", sem);
	fflush(stdout);
	sem->wait();
	printf("Done!\n");
	printf("Waiting again (%p)...", sem);
	fflush(stdout);
	sem->wait();
	printf("GOT'EM COACH!\n");
	
	delete sem;
	return NULL;
}


int main( void ) {
	/*
	sem_t * sem = sem_open("/blah", O_CREAT, 0777, 0);
	pthread_t th;
	pthread_create(&th, NULL, &thread, sem);
	sleep(2);
	sem_post(sem);
	sleep(2);
	sem_unlink("/blah");
	printf("WE OUT!\n");
	sleep(1);
	*/
	void * zmq_ctx = zmq_ctx_new();

	ZMQSemaphore * sem = new ZMQSemaphore(zmq_ctx);

	pthread_t th;
	pthread_create(&th, NULL, &thread, sem);
	sleep(2);
	sem->post();
	printf("Posted...");
	fflush(stdout);
	sleep(2);
	delete sem;
	printf("WE OUT!\n");
	sleep(2);

	printf("Terminating context...");
	fflush(stdout);
	zmq_term(zmq_ctx);
	printf("Done!");
	return 0;
}