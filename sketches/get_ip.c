#include <stdio.h>      
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h> 
#include <string.h> 
#include <arpa/inet.h>
#include <string>

std::string get_link_local_ip6() {
	struct ifaddrs * ifAddrStruct = NULL, * ifa = NULL;

    getifaddrs(&ifAddrStruct);
    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
        if( ifa->ifa_addr->sa_family == AF_INET6 ) {
            char addr[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, addr, INET6_ADDRSTRLEN);
            //printf("%s IP Address %s\n", ifa->ifa_name, addr);

            unsigned int addr_len = strlen(addr);
            char * ll_prefix = strstr(addr, "fe80::");
            if( ll_prefix != NULL && (ll_prefix - addr) < addr_len - 6 ) {
            	freeifaddrs(ifAddrStruct);
            	return std::string(addr);
            }
        }
    }
    if (ifAddrStruct != NULL)
    	freeifaddrs(ifAddrStruct);
    return "";
}

int main(int argc, const char * argv[]) {
	std::string ip = get_link_local_ip6();
	printf("Got link-local ip address: %s (%d)\n", ip.c_str(), ip.size() );
    return 0;
}