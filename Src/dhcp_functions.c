#include <string.h>

#include "main.h"
#include "dhcp_functions.h"
#include "dhcp_container.h"
#include "lwip.h"
#include "udp.h"
#include "etharp.h"
#include "ip_addr.h"

#include <string.h>


extern uint8_t pool_ip_addr[POOL_NUM][2];
extern uint32_t pool_time_rent[POOL_NUM];

extern struct netif gnetif; // interface of ethernet

extern TIM_HandleTypeDef htim2;
extern UART_HandleTypeDef huart3;
extern struct dhcp_container con;
extern uint8_t uart_print[64];

const uint32_t time_rent = 250; // number of seconds which rent to the client

////-----------------------------------------------------------------
// finding the first index after cookie
static int dhcp_options_position(struct pbuf *p)
{
	uint32_t i = 0;

	for (i = 200; i < p->len; i++)
		if (((uint8_t *)p->payload)[i-3] == con.magic_cookie[0] &&
				((uint8_t *)p->payload)[i-2] == con.magic_cookie[1] &&
				((uint8_t *)p->payload)[i-1] == con.magic_cookie[2] &&
				((uint8_t *)p->payload)[i] == con.magic_cookie[3])
		{
			return i + 1;
		}

	return -1;
}

////-----------------------------------------------------------------
// check if the OP == 1, ethernet and 6 length address
static int dhcp_if_it_ethernet(struct pbuf *p)
{
	return (int)(((uint8_t *)p->payload)[0] == 1 && ((uint8_t *)p->payload)[1] == 1 && ((uint8_t *)p->payload)[2] == 6);
}

////-----------------------------------------------------------------
// check if xid equal
static int dhcp_equal_xid(struct pbuf *p)
{
	return (int)(con.xid[1] == ((uint8_t *)p->payload)[4] &&
			con.xid[2] == ((uint8_t *)p->payload)[5] &&
			con.xid[3] == ((uint8_t *)p->payload)[6] &&
			con.xid[4] == ((uint8_t *)p->payload)[7]);
}

////-----------------------------------------------------------------
// save xid to container
static void dhcp_get_xid(struct pbuf *p)
{
	con.xid[0] = 1; // flag
	con.xid[1] = ((uint8_t *)p->payload)[4];
	con.xid[2] = ((uint8_t *)p->payload)[5];
	con.xid[3] = ((uint8_t *)p->payload)[6];
	con.xid[4] = ((uint8_t *)p->payload)[7];
}

////-----------------------------------------------------------------
// save client ip address to container
static void dhcp_get_ciaddr(struct pbuf *p)
{
	con.ciaddr[0] = ((uint8_t *)p->payload)[12];
	con.ciaddr[1] = ((uint8_t *)p->payload)[13];
	con.ciaddr[2] = ((uint8_t *)p->payload)[14];
	con.ciaddr[3] = ((uint8_t *)p->payload)[15];
}

////-----------------------------------------------------------------
// save client MAK address to container
static void dhcp_get_chaddr(struct pbuf *p)
{
	con.chaddr[0] = ((uint8_t *)p->payload)[28];
	con.chaddr[1] = ((uint8_t *)p->payload)[29];
	con.chaddr[2] = ((uint8_t *)p->payload)[30];
	con.chaddr[3] = ((uint8_t *)p->payload)[31];
	con.chaddr[4] = ((uint8_t *)p->payload)[32];
	con.chaddr[5] = ((uint8_t *)p->payload)[33];
}


////-----------------------------------------------------------------
// get ip address from pool
static uint8_t dhcp_get_ip()
{
	static uint32_t index = 0;
	uint8_t ret = 255;

	for (int i = 0; i < POOL_NUM; i++, index++)
	{
		if (index == POOL_NUM)
		{
			index = 0;
		}

		if (!pool_ip_addr[index][0])
		{
			pool_ip_addr[index][0] = 1;
			pool_time_rent[index] = time_rent;
			ret = pool_ip_addr[index][1];
			index++;
			break;
		}
	}

	return ret;
}

////-----------------------------------------------------------------
// renew time of arend
static int dhcp_renew_ip(uint8_t ip)
{
	int ret = -1;

	for (int i = 0; i < POOL_NUM; i++)
	{
		if (pool_ip_addr[i][1] == ip && pool_ip_addr[i][0])
		{
			pool_time_rent[i] = time_rent;
			ret = 0;
			break;
		}
	}

	return ret;
}

////-----------------------------------------------------------------
// release ip
static int dhcp_release_ip(uint8_t ip)
{
	int ret = -1;

	for (int i = 0; i < POOL_NUM; i++)
	{
		if (pool_ip_addr[i][1] == ip)
		{
			pool_ip_addr[i][0] = 0;
			pool_time_rent[i] = 0;
			ret = 0;
			break;
		}
	}

	return ret;
}

////-----------------------------------------------------------------
// if the ip address is free reserve it
static uint8_t dhcp_if_free_reserve_ip(uint8_t ip)
{
	uint8_t ret = 255;

	for (int i = 0; i < POOL_NUM; i++)
	{
		if (pool_ip_addr[i][1] == ip && !pool_ip_addr[i][0])
		{
			pool_ip_addr[i][0] = 1;
			pool_time_rent[i] = time_rent;
			ret = pool_ip_addr[i][1];
			break;
		}
	}

	return ret;
}

////-----------------------------------------------------------------
// stop timer-2 and clear xid
static void dhcp_stop_timer()
{
	HAL_TIM_Base_Stop_IT(&htim2);
	memset(con.xid, 0, sizeof(con.xid));
}


////-----------------------------------------------------------------
// the function is to collect information from the buffer to the container
void dhcp_build_container(struct pbuf *p)
{
	int index = 0;

	for (int i = 0; i < p->len; i++) // debuge
	{
		if (i % 4 == 0)
		{
			sprintf((char *)uart_print, "\n\r");
			HAL_UART_Transmit(&huart3, uart_print, strlen((char *)uart_print), 1000);
		}

		sprintf((char *)uart_print, "%x", ((uint8_t *)p->payload)[i]);
		if (strlen((char *)uart_print) == 1)
			sprintf((char *)uart_print, "0%x", ((uint8_t *)p->payload)[i]);
		HAL_UART_Transmit(&huart3, uart_print, strlen((char *)uart_print), 1000);
	}
	sprintf((char *)uart_print, "\n\rlen: %d\n\r", p->len); //
	HAL_UART_Transmit(&huart3, uart_print, strlen((char *)uart_print), 1000); // debuge

	con.accept_flag = 1;
	memset(con.option_50, 0, sizeof(con.option_50));
	memset(con.option_53, 0, sizeof(con.option_53));
	memset(con.option_55, 0, sizeof(con.option_55));

	if (!dhcp_if_it_ethernet(p))
	{
		con.accept_flag = 0;
		return;
	}

	index = dhcp_options_position(p);
	if (index == -1)
	{
		con.accept_flag = 0;
		return;
	}

	while (((uint8_t *)p->payload)[index] != 0xff && index < p->len) // collecting the options
	{
		switch (((uint8_t *)p->payload)[index])
		{
			case 50: // Requested IP address
				con.option_50[0] = 1; // flag
				con.option_50[1] = ((uint8_t *)p->payload)[index + 2]; //
				con.option_50[2] = ((uint8_t *)p->payload)[index + 3]; //
				con.option_50[3] = ((uint8_t *)p->payload)[index + 4]; //
				con.option_50[4] = ((uint8_t *)p->payload)[index + 5]; // ip address
				break;

			case 53: // DHCP message type
				con.option_53[0] = 1; // flag
				con.option_53[1] = ((uint8_t *)p->payload)[index + 2]; // options
				break;

			case 55: // Parameter request list
				con.option_55[0] = 1; // flag

				for (int i = 0; i < ((uint8_t *)p->payload)[index + 1]; i++) // collecting the request list
				{
					switch (((uint8_t *)p->payload)[index + i + 2])
					{
						case 1: // Subnet mask
							con.option_55[1] = 1;
							break;

						case 3: // Router
							con.option_55[2] = 3;
							break;

						case 6: // Domain name server
							con.option_55[3] = 6;
							break;

						default:
							break;
					}
				}
				break;

			default:
				break;
		}

		index++; //
		index += ((uint8_t *)p->payload)[index] + 1; // position to the next options
	}

	dhcp_get_ciaddr(p);
	dhcp_get_chaddr(p);

	if (!con.option_53[0])
	{
		con.accept_flag = 0;
		return;
	}
	else if (con.option_53[1] == 7) // DHCPRELEASE
	{
		if (con.ciaddr[3] != 0)
		{
			dhcp_release_ip(con.ciaddr[3]);
		}
		else if (con.option_50[0])
		{
			dhcp_release_ip(con.option_50[4]);
		}

		con.accept_flag = 0;
		return;
	}

	if (con.xid[0] && !dhcp_equal_xid(p))
	{
		con.accept_flag = 0;
		return;
	}
	else if (!con.xid[0])
	{
		HAL_TIM_Base_Start_IT(&htim2);
		dhcp_get_xid(p);
	}
}

////-----------------------------------------------------------------
// the function is building the buffer of collected information
int dhcp_build_buffer(struct pbuf *p)
{
	int index = 0;

	((uint8_t *)p->payload)[0] = 2; // OP-from server to the client
	((uint8_t *)p->payload)[1] = 1; // ethernet
	((uint8_t *)p->payload)[2] = 6; // length of MAK address
	((uint8_t *)p->payload)[3] = 0; // hops

	((uint8_t *)p->payload)[4] = con.xid[1];
	((uint8_t *)p->payload)[5] = con.xid[2];
	((uint8_t *)p->payload)[6] = con.xid[3];
	((uint8_t *)p->payload)[7] = con.xid[4];

	((uint8_t *)p->payload)[8] = 0; //
	((uint8_t *)p->payload)[9] = 0; // SECS

	((uint8_t *)p->payload)[10] = 0x80; //
	((uint8_t *)p->payload)[11] = 0;    // broadscast

	((uint8_t *)p->payload)[12] = con.ciaddr[0];
	((uint8_t *)p->payload)[13] = con.ciaddr[1];
	((uint8_t *)p->payload)[14] = con.ciaddr[2];
	((uint8_t *)p->payload)[15] = con.ciaddr[3];

	((uint8_t *)p->payload)[20] = con.siaddr[0];
	((uint8_t *)p->payload)[21] = con.siaddr[1];
	((uint8_t *)p->payload)[22] = con.siaddr[2];
	((uint8_t *)p->payload)[23] = con.siaddr[3];

	((uint8_t *)p->payload)[24] = con.giaddr[0];
	((uint8_t *)p->payload)[25] = con.giaddr[1];
	((uint8_t *)p->payload)[26] = con.giaddr[2];
	((uint8_t *)p->payload)[27] = con.giaddr[3];

	((uint8_t *)p->payload)[28] = con.chaddr[0];
	((uint8_t *)p->payload)[29] = con.chaddr[1];
	((uint8_t *)p->payload)[30] = con.chaddr[2];
	((uint8_t *)p->payload)[31] = con.chaddr[3];
	((uint8_t *)p->payload)[32] = con.chaddr[4];
	((uint8_t *)p->payload)[33] = con.chaddr[5];

	index = 34;

	for (int i = 0; i < 10; i++) // last place for the MAK address
		((uint8_t *)p->payload)[index++] = 0;

	for (int i = 0; i < 192; i++) // BOOTP legacy
			((uint8_t *)p->payload)[index++] = 0;

	((uint8_t *)p->payload)[index++] = con.magic_cookie[0];
	((uint8_t *)p->payload)[index++] = con.magic_cookie[1];
	((uint8_t *)p->payload)[index++] = con.magic_cookie[2];
	((uint8_t *)p->payload)[index++] = con.magic_cookie[3];

	switch (con.option_53[1]) // option type
	{
		case 1: // DHCPDISCOVER
			if (con.option_50[0] && (dhcp_if_free_reserve_ip(con.option_50[4]) != 255)) // request address
			{
				con.yiaddr[3] = con.option_50[4];
			}
			else
			{
				err_t error = ERR_OK;
				ip_addr_t ip;
				int i = 0;

				for (i = 0; i < POOL_NUM; i++)
				{
					con.yiaddr[3] = dhcp_get_ip();
					if (con.yiaddr[3] == 255)
					{
						dhcp_stop_timer();
						return -1;
					}

					IP_ADDR4(&ip, con.yiaddr[0], con.yiaddr[1], con.yiaddr[2], con.yiaddr[3]);
					error = etharp_request(&gnetif, &ip); // check if ip address exists in the net

					if (error == ERR_OK)
					{
						break;
					}
					else
					{
						dhcp_release_ip(con.yiaddr[3]);
					}
				}

				if (i >= 10)
				{
					dhcp_stop_timer();
					return -1;
				}
			}

			((uint8_t *)p->payload)[index++] = 53; // option 53
			((uint8_t *)p->payload)[index++] = 1; // len
			((uint8_t *)p->payload)[index++] = 2; // type DHCPOFFER
			IP_ADDR4(&con.ip_to_send, 0xff, 0xff, 0xff, 0xff);
			break;

		case 3: // DHCPREQUEST
			dhcp_stop_timer();

			if (con.option_50[0] && dhcp_renew_ip(con.option_50[4]) == -1) // if not request option
			{
				return -1;
			}

			con.yiaddr[3] = con.option_50[4];

			((uint8_t *)p->payload)[index++] = 53; // option 53
			((uint8_t *)p->payload)[index++] = 1; // len
			((uint8_t *)p->payload)[index++] = 5; // DHCPACK
			IP_ADDR4(&con.ip_to_send, 0xff, 0xff, 0xff, 0xff);
			break;

		case 8: // DHCPINFORM
			dhcp_stop_timer();

			((uint8_t *)p->payload)[index++] = 53; // option 53
			((uint8_t *)p->payload)[index++] = 1; // len
			((uint8_t *)p->payload)[index++] = 5; // DHCPACK
			 IP_ADDR4(&con.ip_to_send, con.ciaddr[0], con.ciaddr[1], con.ciaddr[2], con.ciaddr[3]);
			break;

		default:
			return -1;
	}

	if (con.option_53[1] == 8) // if type DHCPINFORM
	{
		((uint8_t *)p->payload)[16] = 0;
		((uint8_t *)p->payload)[17] = 0;
		((uint8_t *)p->payload)[18] = 0;
		((uint8_t *)p->payload)[19] = 0;
	}
	else
	{
		((uint8_t *)p->payload)[16] = con.yiaddr[0];
		((uint8_t *)p->payload)[17] = con.yiaddr[1];
		((uint8_t *)p->payload)[18] = con.yiaddr[2];
		((uint8_t *)p->payload)[19] = con.yiaddr[3];
	}


	if (con.option_55[0])
	{
		if (con.option_55[1] == 1) // Subnet mask
		{
			((uint8_t *)p->payload)[index++] = 1; // comand
			((uint8_t *)p->payload)[index++] = 4; // len

			((uint8_t *)p->payload)[index++] = con.mask[0]; //
			((uint8_t *)p->payload)[index++] = con.mask[1]; //
			((uint8_t *)p->payload)[index++] = con.mask[2]; //
			((uint8_t *)p->payload)[index++] = con.mask[3]; // Subnet mask
		}

		if (con.option_55[2] == 3) // Router
		{
			((uint8_t *)p->payload)[index++] = 3; // comand
			((uint8_t *)p->payload)[index++] = 4; // len

			((uint8_t *)p->payload)[index++] = con.giaddr[0]; //
			((uint8_t *)p->payload)[index++] = con.giaddr[1]; //
			((uint8_t *)p->payload)[index++] = con.giaddr[2]; //
			((uint8_t *)p->payload)[index++] = con.giaddr[3]; // Router
		}

		if (con.option_55[3] == 6) // Domain name server
		{
			((uint8_t *)p->payload)[index++] = 6; // comand
			((uint8_t *)p->payload)[index++] = 4; // len

			((uint8_t *)p->payload)[index++] = con.domain_name_server[0]; //
			((uint8_t *)p->payload)[index++] = con.domain_name_server[1]; //
			((uint8_t *)p->payload)[index++] = con.domain_name_server[2]; //
			((uint8_t *)p->payload)[index++] = con.domain_name_server[3]; // Domain name server
		}
	}

	if (con.option_53[1] != 8) // if type DHCPINFORM
	{
		((uint8_t *)p->payload)[index++] = 51; // time arend ip
		((uint8_t *)p->payload)[index++] = 4;  // len
		((uint8_t *)p->payload)[index++] = ((uint8_t *)(&time_rent))[3];
		((uint8_t *)p->payload)[index++] = ((uint8_t *)(&time_rent))[2];
		((uint8_t *)p->payload)[index++] = ((uint8_t *)(&time_rent))[1];
		((uint8_t *)p->payload)[index++] = ((uint8_t *)(&time_rent))[0];
	}


	((uint8_t *)p->payload)[index++] = 0xff; // end of options

	for (int i = 0; i < 300; i++) // length must be 300
		((uint8_t *)p->payload)[index++] = 0;

	p->len = index;

	return 0;
}



