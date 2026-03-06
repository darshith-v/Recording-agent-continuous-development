#ifndef _PTC_INTF_H
#define _PTC_INTF_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "ptc_config.h"

#include "ptc_api.h"
#include "video_api.h"

/* function prototypes */
extern int ptci_up_meta(int argc, char *argv[], char *name) ;
extern int ptci_get_rm_meta() ;
extern int ptci_data_lock() ;
extern void ptci_data_unlock() ;
extern void ptci_set_rm_meta(int rm) ;

#endif
