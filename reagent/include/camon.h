#ifndef _CAMON_H_
#define _CAMON_H_

#include "reagent_config.h"

#define ONVIF_PROBE_MCADDR "239.255.255.250"
#define ONVIF_PROBE_IPPORT 3702
#define ONVIF_PROBE_NIC	"br0"

/*
 * camera monitor 'time to live' -- don't bother going offline until this TTL counter is still >0
 */
#define CAMON_TTL 6

/*
 * information collected from ONVIF camera responses
 */
typedef struct {
	char ip_addr[16] ;
	char mac_addr[16] ;
	int is_alive ;
} camon_status_t ;

extern int camon_init(char *sip) ;
extern void camon_deinit(int sock) ;
extern int camon_perform_tx(int sock) ;
extern int camon_perform_rx(int sock, camon_status_t *p_status, int maxcam, media_info_t *p_known, int n_known) ;

#endif
