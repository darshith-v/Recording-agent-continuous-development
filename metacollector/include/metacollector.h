#ifndef _META_COLLECTOR_H
#define _META_COLLECTOR_H

#include <stdbool.h>

#include "metadata_def.h"


typedef struct 
{
	char storage_location[128];			/* storage location */
	char loco_id[16];					/* loco id */
	char subpath[128];					/* subpath, METADATA or camera name and mac address */
	unsigned long start_time;			/* start time in epoch seconds */
	unsigned long end_time;				/* end time in epoch seconds */
	bool gps_enabled; 					/* whether gps retrival is enable or not */
	bool event_enabled; 				/* whether video event retrival is enable or not */
	bool timeoffset_enable;				/* whether time offset retrival is enable or not */
	bool locoId_enable;					/* whether locoId retrival is enable or not */
	bool validate_hash;					/* flag for hash validation */
	char file[256];						/* input/output file path */
} collector_input_chan_t;

#endif