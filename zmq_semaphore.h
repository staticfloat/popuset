#ifndef ZMQ_SEMAPHORE_H
#define ZMQ_SEMAPHORE_H

#include <zmq.h>

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


#endif // ZMQ_SEMAPHORE_H