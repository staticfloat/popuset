#include <stdio.h>
#include <stdlib.h>
#include "mDNS.h"

// This guy doesn't do much other than redirect the callbacks back to our internal callback function
static void browse_callback(
	DNSServiceRef service,
	DNSServiceFlags flags,
	uint32_t interfaceIndex,
	DNSServiceErrorType errorCode,
	const char * name,
	const char * type,
	const char * domain,
	void * context)
{
	((mDNS *)context)->browsed(service, flags, interfaceIndex, errorCode, name, type, domain);
}

// Yet another friend!
static void resolve_callback(
	DNSServiceRef service,
	DNSServiceFlags flags,
	uint32_t interfaceIndex,
	DNSServiceErrorType errorCode,
	const char *fullname,
	const char *host,
	uint16_t port,
	uint16_t txtLen,
	const unsigned char *txt,
	void *context)
{
	((mDNS *)context)->resolved(service, flags, interfaceIndex, errorCode, fullname, host, ntohs(port), txtLen, txt);
}


// Processes events just as fast as its pathetic little arms can.
static void * runloop(void * context) {
	DNSServiceRef browseRef = ((mDNS *) context)->getServiceRef();
	while( true ) {
		DNSServiceErrorType error = DNSServiceProcessResult(browseRef);
		switch( error ) {
			case kDNSServiceErr_NoError:
				break;
			default:
				printf("Unknown error from DNSServiceProcessResult(): %d", error);
				break;
		}
	}
	return NULL;
}


mDNS::mDNS(const char * serviceType) {
    // Set no flags, look on all interfaces for the given service type, across all (default) domains,
    // calling browse_callback with a pointer to ourselves so that callback() can be looped in.
    DNSServiceErrorType err = DNSServiceBrowse(&browseRef, 0, 0, serviceType, NULL, browse_callback, this);
    if( err != 0 ) {
    	printf("Could not initiate browsing:\n");
    	print_error(err);
    	throw "Could not initiate browsing!";
    }

    // Create polling thread
    printf("Creating thread...\n");
    if( pthread_create(&browseThread, NULL, &runloop, this) != 0 ) {
    	DNSServiceRefDeallocate(browseRef);
    	throw "Could not start browsing thread!";
    }

    this->serviceType = new char[strlen(serviceType)+1];
    strcpy(this->serviceType, serviceType);
}

void remove_mDNSService(std::map<const char *, mDNSService *, cmp_str> & map, const char * name) {
	std::map<const char *, mDNSService *, cmp_str>::iterator itty = map.find(name);
	if( itty == map.end() )
		return;
	mDNSService * del = (*itty).second;
	map.erase(itty);

	if( del->serviceRef != NULL)
		DNSServiceRefDeallocate(del->serviceRef);
	delete[] del->fullname;
	delete[] del->host;
	delete[] del->txt;
	delete del;
}

mDNS::~mDNS() {
	while( !this->services.empty() )
		remove_mDNSService(this->services, this->services.begin()->first);

	while( !this->registrations.empty() )
		remove_mDNSService(this->registrations, this->registrations.begin()->first);

	// Deallocate the reference, which should convince the thread to kill itself
	DNSServiceRefDeallocate(browseRef);

	// Ensure that our browsing thread ends itself
	pthread_join(browseThread, NULL);
}

void mDNS::registerService(const char * name, unsigned short port) {
	if( registrations.find(name) != registrations.end() ) {
		printf("Service %s already exists!\n", name);
		return;
	}

	DNSServiceRef service;
	DNSServiceErrorType err;
	err = DNSServiceRegister(&service, 0, 0, name, this->serviceType, NULL, NULL, htons(port), 0, NULL, NULL, NULL );

	if( err != kDNSServiceErr_NoError ) {
		printf("Could not register %s:%d", name, port);
		print_error(err);
	} else {
		// Grab this service, stuff it into an mDNSService object, and register it in our list
		mDNSService * newService = new mDNSService();
		newService->serviceRef = service;
		newService->fullname = new char[strlen(name)+1];
		strcpy(newService->fullname, name);
		newService->host = new char[strlen("localhost")+1];
		strcpy(newService->host, "localhost");
		newService->port = htons(port);
		newService->txt = NULL;
		newService->txtLen = 0;

		this->registrations[newService->fullname] = newService;
	}
}

void mDNS::deregisterService(const char * name) {
	if( registrations.find(name) == registrations.end() )
		return;

	remove_mDNSService(this->registrations, name);
}

const mDNSService * mDNS::resolveService(const char * name, const char * type, const char * domain, int timeout ) {
	printf("Attempting to resolve %s.%s%s\n", name, type, domain);
	DNSServiceRef resolve_ref;
	DNSServiceErrorType err = DNSServiceResolve(&resolve_ref, 0, 0, name, type, domain, resolve_callback, this);
	if( err != 0 ) {
		printf("Could not begin resolving %s.%s%s:\n", name, type, domain);
		print_error(err);
		return NULL;
	}

	// Here we attempt to resolve for a maximum of 5 seconds, then give up.
	int sock = DNSServiceRefSockFD(resolve_ref);
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);
	
	struct timeval tv;
	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	int retval = select(sock+1, &rfds, NULL, NULL, &tv);
	if( retval <= 0 ) {
		return NULL;
	}

	DNSServiceErrorType error = DNSServiceProcessResult(resolve_ref);
	if( error != kDNSServiceErr_NoError ) {
		printf("Could not finish resolving %s.%s%s: %d\n", name, type, domain, error);
		print_error(err);
		return NULL;
	}
	char * fullname = new char[strlen(name)+1+strlen(type)+strlen(domain)+1];
	sprintf(fullname, "%s.%s%s", name, type, domain);
	return services[fullname];
}

void mDNS::browsed(DNSServiceRef service, DNSServiceFlags flags, uint32_t interfaceIndex,
	DNSServiceErrorType errorCode, const char * name, const char * type, const char * domain) {
	resolveService(name, type, domain);
}

void mDNS::resolved(DNSServiceRef service, DNSServiceFlags flags, uint32_t interfaceIndex,
	DNSServiceErrorType errorCode, const char *fullname, const char *host,
	uint16_t port, uint16_t txtLen, const unsigned char *txt) {

	// Grab this service, stuff it into an mDNSService object, and register it in our list
	mDNSService * newService = new mDNSService();
	newService->serviceRef = NULL;
	newService->fullname = new char[strlen(fullname)+1];
	strcpy(newService->fullname, fullname);
	newService->host = new char[strlen(host)+1];
	strcpy(newService->host, host);
	newService->port = port;
	newService->txt = new unsigned char [txtLen];
	memcpy(newService->txt, txt, txtLen);
	newService->txtLen = txtLen;

	// Register it in our list
	this->services[newService->fullname] = newService;
	DNSServiceRefDeallocate(service);

	printf("Resolved a service! %s %s %d\n", fullname, host, port);
}

void mDNS::print_error(DNSServiceErrorType error) {
	switch( error ) {
		case kDNSServiceErr_NoError:
			break;
		case kDNSServiceErr_BadParam:
			printf("[%d] Bad Parameters\n", error);
			break;
		default:
			printf("Unknown error: %d", error);
			break;
	}
}

DNSServiceRef mDNS::getServiceRef() {
	return this->browseRef;
}



int main(void) {
	mDNS * mdns = new mDNS("_ssh._tcp.");
	mdns->registerService("test", 5040);
	int c = 0;
	while( c < 6 ) {
		sleep(1);
		c++;
	}
	printf("Deregistering service....\n");
	mdns->deregisterService("test");

	while(true) {
		sleep(1);
		mdns->resolveService("test2", "_ssh._tcp.", "local.");
		mdns->resolveService("test", "_ssh._tcp.", "local.");
	}
}