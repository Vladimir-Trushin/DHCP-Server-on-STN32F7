#ifndef _DHCP_FUNCTIONS_
#define _DHCP_FUNCTIONS_

#include "pbuf.h"
#include "dhcp_container.h"

void dhcp_build_container(struct pbuf *p);
int dhcp_build_buffer(struct pbuf *p);


#endif
