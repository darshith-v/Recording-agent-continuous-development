#ifndef _META_READER_H
#define _META_READER_H

#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 

#include "metadata_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#define METADATA_CHUNK_MAX_INDEX_RECORDS 		128

typedef struct
{
    uint32_t 			record_time;
    meta_time_memory_t		data;
} timeoffset_record_t;

typedef struct
{
    uint32_t 			record_time;
    meta_gps_memory_t		data;
} gps_record_t;

typedef struct
{
    uint32_t 			record_time;
    meta_event_recording_t 	data;
} video_event_record_t;

typedef struct
{
    uint32_t 			record_time;
    meta_loco_id_t 	    data;
} locomotive_id_record_t;

extern const char* get_digital_config_name ( FILE *fp, int dig_num);				
									  

extern int get_metadata_records ( FILE *fp, 				
								  unsigned long starttime, 
								  unsigned long endtime, 
								  eMetaDataType type, 
								  void *precords, 
								  int *rec_cnt 
							 	);

#ifdef __cplusplus
}
#endif

#endif