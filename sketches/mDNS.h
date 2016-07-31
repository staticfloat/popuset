#ifndef MDNS_H
#define MDNS_H

#include <dns_sd.h>
#include <map>
#include <list>
#include <pthread.h>

struct mDNSService {
	char * fullname;
	char * host;
	uint16_t port;
	unsigned char * txt;
	uint16_t txtLen;
	DNSServiceRef serviceRef;
};

// String comparator for our map
struct cmp_str
{
   bool operator()(const char *a, const char *b) const
   {
      return std::strcmp(a, b) < 0;
   }
};

class mDNS {
public:
	mDNS(const char * serviceType = "_ssh._tcp");
	~mDNS();

	// Register/deregister services we offer on this computer
	void registerService(const char * name, unsigned short port);
	void deregisterService(const char * name);

	// Resolve a service, either to ensure that it's still running, or bceause we've never seen it before
	const mDNSService * resolveService(const char * name, const char * type, const char * domain, int timeout = 2);

	// The callback that gets hit every time we encounter a new service
	void browsed(DNSServiceRef service,
		DNSServiceFlags flags,
		uint32_t interfaceIndex,
		DNSServiceErrorType errorCode,
		const char * name,
		const char * type,
		const char * domain
	);

	void resolved(
		DNSServiceRef service,
		DNSServiceFlags flags,
		uint32_t interfaceIndex,
		DNSServiceErrorType errorCode,
		const char *fullname,
		const char *host,
		uint16_t port,
		uint16_t txtLen,
		const unsigned char *txt
	);

	DNSServiceRef getServiceRef();
protected:
	void print_error(DNSServiceErrorType err);

	char * serviceType;
	DNSServiceRef browseRef;
	pthread_t browseThread;

	// Map name to mDNSService for services that we've got completely resolved
	std::map<const char *, mDNSService *, cmp_str> services;
	std::map<const char *, mDNSService *, cmp_str> registrations;
};


#endif //MDNS_H