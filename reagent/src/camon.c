#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <time.h>
#include <syslog.h>
#include <sys/random.h>
#include <netdb.h>

#ifndef QNX_BUILD
#include <linux/sockios.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#endif

#include <net/if.h>
#include <net/if_arp.h>
#include <arpa/inet.h>

#include "reagent_config.h"
#include "camon.h"

static const char probe_req1[] =
	"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
	"<Envelope xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" xmlns=\"http://www.w3.org/2003/05/soap-envelope\">"
	"<Header>"
	"<wsa:MessageID xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">uuid:%s</wsa:MessageID>"
	"<wsa:To xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</wsa:To>"
	"<wsa:Action xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</wsa:Action>"
	"</Header>"
	"<Body>"
	"<Probe xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\">"
	"<Types>tds:Device</Types>"
	"<Scopes />"
	"</Probe>"
	"</Body>"
	"</Envelope>";

static const char probe_req2[] =
	"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
	"<Envelope xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\" xmlns=\"http://www.w3.org/2003/05/soap-envelope\">"
	"<Header>"
	"<wsa:MessageID xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">uuid:%s</wsa:MessageID>"
	"<wsa:To xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</wsa:To>"
	"<wsa:Action xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</wsa:Action>"
	"</Header>"
	"<Body>"
	"<Probe xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\">"
	"<Types>dn:NetworkVideoTransmitter</Types>"
	"<Scopes />"
	"</Probe>"
	"</Body>"
	"</Envelope>";

/*
 * returns IPv4 address of a network interface (poited by 'p')
 * in a binary form, network byte order.
 * INADDR_ANY is returned on failure
 */
static unsigned int get_iface_ipaddr(unsigned char *p)
{
    unsigned long ipaddr = INADDR_ANY ;

    char buf [256];
    struct ifconf ic;
    int i, n;
    int sock;
    int address_count = 0;
    struct ifreq *pir ;
    struct sockaddr_in *psin ;

    /* Create an unbound datagram socket to do the SIOCGIFADDR ioctl on. */
    if ((sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        return ipaddr ;
    }

    ic.ifc_len = sizeof buf;
    ic.ifc_ifcu.ifcu_buf = (caddr_t)buf;

    n = ioctl(sock, SIOCGIFCONF, &ic);

    if (n >= 0) {
        pir = ic.ifc_req ;
        n = ic.ifc_len / sizeof(struct ifreq) ;

        for (pir = ic.ifc_req, i = 0 ; i < n; i++, pir++) {
            if ((strcasecmp((const char *) p, (const char *)(pir->ifr_name))) == 0) {
				if (pir->ifr_addr.sa_family == AF_INET) {
					memcpy(&ipaddr, &pir->ifr_addr.sa_data[2], sizeof(ipaddr)) ;    // skip sin_family member
					break ;
				}
            }
        }
    }

    close(sock) ;

    return ipaddr ;
}

int genRandNum()
{
	int rc = -1;

	while(rc < 0)
	{
		int randData = 1;
		if (getrandom(&randData, sizeof(unsigned short), GRND_NONBLOCK) <= 0)
		{
			syslog(LOG_WARNING, "%s: Error generating random number at line (%d).\n", __func__, __LINE__);
		}
		else if(randData < 0) //need a number that's >= 0
		{
			syslog(LOG_WARNING, "%s: Error generating random number at line (%d).\n", __func__, __LINE__);
		}
		else
		{
			rc = randData;
		}
		usleep(1);
	}

	return rc;
}

/*
 * prumitive UUID generator
 */
static char *uuid_create(char *uuid, int len)
{
     //using getrandom func which is crypto-safe
	snprintf(uuid, len, "%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
			 genRandNum() % 0xFFFF, genRandNum() % 0xFFFF, genRandNum() % 0xFFFF, genRandNum() % 0xFFFF,
			 genRandNum() % 0xFFFF, genRandNum() % 0xFFFF, genRandNum() % 0xFFFF, genRandNum() % 0xFFFF);

	return uuid;
}

/*
 * transmits a standard ONVIF probe request using a socket provided.
 * Note that some older camera models do not respond to the very basic
 * 'probe Device' query, so we have to send a 'probe NetworkVideoTransmitter' query in addition to it.
 */
static int probe_req_tx(int fd)
{
	int len = 0;
	int rlen = 0;
	char uuid[100] = {'\0'};
	char  * p_bufs = NULL;
	struct sockaddr_in addr;

	int buflen = 10*1024;

	p_bufs = (char *)malloc(buflen);
	if (NULL == p_bufs)
	{
		return -1;
	}

	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ONVIF_PROBE_MCADDR);
	addr.sin_port = htons(ONVIF_PROBE_IPPORT);

	memset(p_bufs, 0, buflen);
	sprintf(p_bufs, probe_req1, uuid_create(uuid, sizeof(uuid)));

	len = (int)strlen(p_bufs);
	rlen = sendto(fd, p_bufs, len, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));

    usleep(1000);

    memset(p_bufs, 0, buflen);
	sprintf(p_bufs, probe_req2, uuid_create(uuid, sizeof(uuid)));

	len = (int)strlen(p_bufs);
	rlen = sendto(fd, p_bufs, len, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));

	free(p_bufs);

	return rlen;
}
/*
 * receives responses to 'probe' queries sent back by the active ONVIF devices on the network
 */
static int probe_net_rx(int fd, char *rbuf, int maxrbuf, struct sockaddr_in *peer, int maxpeer)
{
	int i;
    int ret;
    int	maxfd = 0;
    fd_set fdread;
    struct timeval tv ;
	int rlen = 0 ;
	char mybuf[8096] ;
	char *p ;
	int read_length ;
	int npeers = 0 ;

    FD_ZERO(&fdread);

	if (fd > 0) {
		FD_SET(fd, &fdread);
		maxfd = fd ;
	}

    tv.tv_sec = 0 ;
    tv.tv_usec  = 400000 ;	/* 300 milliseconds should be enough for all cameras to respond */
    ret = select(maxfd+1, &fdread, NULL, NULL, &tv);

    if (ret == 0) {// Time expired
        return 0;
    }

	if ((fd > 0) && (FD_ISSET(fd, &fdread)))
	{
		int addr_len;
		struct sockaddr_in addr;

		if (rbuf != NULL) {
			p = rbuf ;
			read_length = maxrbuf-1 ;
		}
		else {
			p = &mybuf[0] ;
			read_length = sizeof(mybuf)-1 ;
		}

		addr_len = sizeof(struct sockaddr_in);
		for (;npeers < maxpeer; ) {
			rlen = recvfrom(fd, p, read_length, 0, (struct sockaddr *)&addr, (socklen_t*)&addr_len);
			if (rlen <= 0) {
				*p = '\0' ;
				break ;
			}
			else {
				p[rlen] = '\0';

				/* see if this IP address is already known. Ignore it, if so */
				for (i=0; i < npeers; i++) {
					if (memcmp(&peer[i], &addr, sizeof(addr)) == 0) {
						break ;
					}
				}

				if (i >= npeers) {
					peer[npeers] = addr ;
					npeers += 1 ;
				}
			}
		}
	}

	return npeers ;
}

static char* mac_ntoa(unsigned char *ptr) {
	static char address[30];
	sprintf(address, "%02X%02X%02X%02X%02X%02X", ptr[0], ptr[1], ptr[2],
			ptr[3], ptr[4], ptr[5]);
	return (address);
}

/*
 * scans the 'pif' network interface's ARP table for an IP address specified by 'host'
 * on Success: returns 0, the buffer pointed by 'pmac' receives the corresponding MAC address
 * Returns -1 on failure: IP address not found or error occured
 */
static int arptable_lookup(char *pif, char *host, unsigned char *pmac)
{
	int s = 0;
	struct arpreq req;
	struct hostent *hp;
	struct sockaddr_in *sin;
	int rc = 0 ;

	if((pif == NULL) || (host == NULL) || (pmac == NULL))
	{
		return -1;
	}

	bzero((caddr_t) &req, sizeof(req));

	sin = (struct sockaddr_in*) &req.arp_pa;
	sin->sin_family = AF_INET; /* Address Family: Internet */
	sin->sin_addr.s_addr = inet_addr(host);

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket() failed.");
		rc = -1 ;
	}
	else {
#ifndef QNX_BUILD
		memcpy(req.arp_dev, pif, sizeof(req.arp_dev));
#endif

		if (ioctl(s, SIOCGARP, (caddr_t) &req) < 0) {
			if (errno == ENXIO) {
				rc = -1 ;
			} else {
	////			perror("SIOCGARP");
				rc = -1 ;
			}
		}
		close(s); /* Close the socket, we don't need it anymore. */

		if (pmac) {
			if (rc == 0) {
				memcpy(pmac, &(req.arp_ha.sa_data), 6) ;
			}
		}
	}

#if 0
	printf("%s (%s) at ", host, inet_ntoa(sin->sin_addr));

	if (req.arp_flags & ATF_COM) {
		printf("%s ", mac_ntoa(req.arp_ha.sa_data));
	} else {
		printf("incomplete");
	}

	if (req.arp_flags & ATF_PERM) {
		printf("ATF_PERM");
	}
	if (req.arp_flags & ATF_PUBL) {
		printf("ATF_PUBL");
	}
	if (req.arp_flags & ATF_USETRAILERS) {
		printf("ATF_USETRAILERS");
	}

	printf("\n");
#endif

	return (rc);
}

static inline int socket_setnonblock(int sock, int noblock)
{
	// 0-block, 1-no-block
	// http://stackoverflow.com/questions/1150635/unix-nonblocking-i-o-o-nonblock-vs-fionbio
	// Prior to standardization there was ioctl(...FIONBIO...) and fcntl(...O_NDELAY...) ...
	// POSIX addressed this with the introduction of O_NONBLOCK.
	int flags = fcntl(sock, F_GETFL, 0);
	return fcntl(sock, F_SETFL, noblock ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
	//return ioctl(sock, FIONBIO, &noblock);
}

/*
 * prepare camera monitor socket:
 * - open and bind to a specified interface
 * - join the ONVIF probe multicast group
 * returns: new socket on success, -1 in case of failure
 */
int camon_init(char *sip)
{
	int opt = 1;
	int loop = 0;
	int fd = -1;
	struct sockaddr_in send_iface;
	struct sockaddr_in addr;
	struct ip_mreq mcast;

	unsigned int ip = 0;
	int rc = -1;

	if (isdigit(sip[0])) {
	    ip = inet_addr(sip) ;
	}
	else {
	    ip = get_iface_ipaddr((unsigned char *) sip) ;
	}

	if (ip != INADDR_ANY) {
		fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if(fd >- 0) {
			addr.sin_family = AF_INET;
			addr.sin_port = htons(ONVIF_PROBE_IPPORT /*INADDR_ANY*/);
			addr.sin_addr.s_addr = ip ;

			/* reuse socket addr */
			opt = 1;
			//capturing return here, as it may fail
			if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
			{
				syslog(LOG_WARNING, "%s: setsockopt failed (%d).\n", __func__, errno);
			}

			/* disallow multicast looping */
			opt = 0 ;
			if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char*)&opt, sizeof(opt)) < 0) {
				perror("IP_MULTICAST_LOOP") ;
			}

			memset(&send_iface, 0, sizeof(send_iface));
			send_iface.sin_family = AF_INET;
			send_iface.sin_port = htons(ONVIF_PROBE_IPPORT) ;
			send_iface.sin_addr.s_addr = ip ;

			if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &send_iface.sin_addr.s_addr, 4) < 0) {
				perror ("IP_MULTICAST_IF setsockopt");
			}

			if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
				// if port 3702 already occupied, only receive unicast message
				addr.sin_port = 0;
				if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
					close(fd);
					return rc;
				}
			}

			// setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char*)&loop, sizeof(loop));

			memset(&mcast, 0, sizeof(mcast));
			mcast.imr_multiaddr.s_addr = inet_addr(ONVIF_PROBE_MCADDR);
			mcast.imr_interface.s_addr = ip;

			if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mcast, sizeof(mcast)) < 0) {
				close(fd);
				fd = -1 ;
			}

			if (fd >= 0) {
     			//capturing return here, as it may fail
				if(socket_setnonblock(fd, 1) >= 0)
				{
					rc = fd;
				}
				else
				{
					syslog(LOG_WARNING, "%s: socket_setnonblock failed at line (%d).\n", __func__, __LINE__);
					rc = -1;
				}
			}
		}
	}
	return (rc) ;
}

/*
 * close socket used to monitor the camera
 */
void camon_deinit(int fd)
{
	if (fd >= 0) {
		close(fd) ;
	}
}


/*
 * camera monitor perform
 *
 * _tx sends the ONVIF service probe message(s) using a preliminary prepared socket
 *
 * _rx counts responses of available ONVIF devices until that count reaches 'maxcams' or the
 * next response packet is not available
 */
/*
 * ONVIF probe handshake is a lengthy process where the time spent between the probe request
 * and actual camera responses can vary in a wide range. This behavior can impact the PTC 1Hz callback events
 * which we prefer to use.
 * The following technique is used in order to minimize the possible impact:
 * - send probe request on every 'even' pass of a 1Hz loop, collect camera responses on every 'odd' loop pass.
 * the 'camera online' information collected during the 'Receive' pass will be processed during the next 'Transmit' loop pass
 */

int camon_perform_tx(int sock)
{
	if (sock >= 0)
	{
		if(probe_req_tx(sock) < 0)//capturing return here, as it may fail
		{
			syslog(LOG_ERR, "%s: probe_req_tx failed.\n", __func__);
		}
	}
	return 0 ;
}

static int is_camera_available(char *phomepg, int tmo_ms, char *pout, int maxsize)
{
	int rc = 0;
	unsigned short port = 0;
	char *addr = NULL;
	struct sockaddr_in address;
	short int sock = -1;
	fd_set fdset;
	struct timeval tv;
	int so_error = -1;

	char tmp[80] = {0};
	char *p = NULL;

	if (phomepg != NULL) {
		if((p = strstr(phomepg, "//")) == NULL)//capturing return here, as it may fail
		{
			return rc;
		}
		p += 2;
		strncpy(tmp, p, sizeof(tmp)-1) ;
		if ((p=strchr(tmp, '/')) != NULL) {
			*p = '\0' ;
		}
		if ((p=strchr(tmp, ':')) != NULL) { // see if port specified
			*p++ = '\0' ;
			port = (unsigned short) atoi(p) ;
		}
		else {
			port = 80 ;
		}
		addr = &tmp[0] ;

		address.sin_family = AF_INET;
		address.sin_addr.s_addr = inet_addr(addr);
		address.sin_port = htons(port);

		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock >= 0) {
			if(fcntl(sock, F_SETFL, O_NONBLOCK) < 0)//capturing return here, as it may fail
			{
				close(sock);
				return rc;
			}

			if(connect(sock, (struct sockaddr *)&address, sizeof(address)) < 0)
			{
				close(sock);
				return rc;
			}

			FD_ZERO(&fdset);
			FD_SET(sock, &fdset);
			tv.tv_sec = 0;
			tv.tv_usec = tmo_ms * 1000;

			if (select(sock + 1, NULL, &fdset, NULL, &tv) == 1)
			{
				socklen_t len = sizeof so_error;
				getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
				if (so_error == 0) {
					if (pout) {
						strncpy(pout, tmp, maxsize-1) ;
					}
					rc = 1 ;
				}
			}

			close(sock);
		}
	}

	return rc ;
}

int camon_perform_rx(int sock, camon_status_t *p_status, int maxcams, media_info_t * p_known, int n_known)
{
	int i = 0, j = 0, ncams = 0;
	struct sockaddr_in peer ;
	struct sockaddr_in cams[maxcams] ;
	camdef_t *p_camdef ;

	unsigned char tmpmac[6] = {0};

	if (sock >= 0) {

		memset(p_status, 0, maxcams * sizeof(camon_status_t)) ;

		ncams = probe_net_rx(sock, NULL, 0, &cams[0], maxcams ) ;
		////syslog(LOG_INFO, "---- %d cameras responded, %d known\n", ncams, n_known) ;
		for (i=0; i < ncams; i++) {
			memcpy(&p_status[i].ip_addr[0], inet_ntoa(cams[i].sin_addr), sizeof(p_status[i].ip_addr));
#ifdef CAMON_DO_ARPTABLE_LOOKUP
			p_status[i].is_alive = (arptable_lookup(ONVIF_PROBE_NIC, &p_status[i].ip_addr[0], &tmpmac[0]) == 0) ? 1 : 0 ;
			if (p_status[i].is_alive) {
				strcpy(&p_status[i].mac_addr[0], mac_ntoa(&tmpmac[0])) ;
			}
#else
			for (j = 0; j < n_known; j++) {
				p_camdef = &p_known->cameras[j] ;
				if (strstr(p_camdef->homepage, &p_status[i].ip_addr[0]) != NULL) {
					p_status[i].is_alive = 1 ;
					strncpy(&p_status[i].mac_addr[0], p_camdef->disp_mac, sizeof(p_status[0].mac_addr) - 1) ;
					////syslog(LOG_INFO, "%s %s is alive\n", &p_status[i].ip_addr[0], &p_status[i].mac_addr[0]) ;
					break ;
				}
			}
#endif

		}
	}
	else { // No ONVIF probe possible, try the alternative way
		char *pout = NULL;
		int maxsize = 0;
		ncams = 0 ;
		for (j = 0; j < n_known; j++) {
			p_camdef = &p_known->cameras[j] ;
			pout = &p_status[ncams].ip_addr[0] ;
			maxsize = sizeof(p_status[0].ip_addr) ;
			if (is_camera_available(p_camdef->homepage, 200, pout, maxsize) != 0) {
				p_status[ncams].is_alive = 1 ;
				strncpy(&p_status[ncams].mac_addr[0], p_camdef->disp_mac, sizeof(p_status[0].mac_addr) - 1) ;
				ncams += 1 ;
			}
		}
	}

	return ncams ;
}

