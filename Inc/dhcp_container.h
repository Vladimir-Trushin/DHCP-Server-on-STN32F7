#ifndef _DHCP_CONTAINER_
#define _DHCP_CONTAINER_

#include "ip_addr.h"


#define POOL_NUM 10 // number of ip addresses

struct dhcp_container
{
	uint8_t accept_flag; // if collection information is cusses

	uint8_t xid[5];
	uint8_t ciaddr[4];
	uint8_t yiaddr[4];
	uint8_t siaddr[4];
	uint8_t giaddr[4];
	uint8_t chaddr[6];
	uint8_t mask[4];
	uint8_t domain_name_server[4];

	uint8_t magic_cookie[4];

	ip_addr_t ip_to_send;

	uint8_t option_53[2]; // DHCP message type [0] - flag, [1] - type
	uint8_t option_50[5]; // Requested IP address [0] - flag, [1-4] - ip
	uint8_t option_55[4]; // Parameter request list [0] - flag, [1] - subnet mask, [2] - router, [3] - domain name server
};




#endif
