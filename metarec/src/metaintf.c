#include <stdio.h> 
#include <string.h> 
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <syslog.h>

#include "video_api.h"
#include "metadata_def.h"
#include "metadata_util.h"
#include "metaqueue.h"
#include "metaintf.h"
#include "metaio.h"

static metadata_rec_t all_metadata;
static meta_desc_q_t metadata_queue; 

static digital_input_descriptor_t digital_events_config_map[NUM_DIGITAL_INPUTS] = 
{
	//name  number  recording
	{ "",	 0, 	 false },
	{ "",	 1, 	 false },
	{ "", 	 2, 	 false } 
};


/* local function prototypes */
static int enq_metadata_descriptor(metadata_desc_t* pdesc);
static metadata_desc_t* deq_metadata_descriptor();


static int enq_metadata_descriptor(metadata_desc_t* pdesc)
{
	int rc = 0;
	if (meta_q_lock(&metadata_queue, 1) == 0) {
		if (meta_q_wait_for_room(&metadata_queue) == 0) {
#if 1
		    {
		        void *p = malloc((pdesc->data_length + 32) & 0xfffffff0) ;  // allocate integral number of 16-byte blocks, large enoung ...
                memcpy(p, pdesc->pData, pdesc->data_length ) ;
                pdesc->pData = p ;
		    }
#endif
		    meta_q_enq(&metadata_queue, (void *)pdesc) ;

			rc = 1;
		}
		meta_q_unlock((void *)&metadata_queue, 1) ;
	}
	return rc;
}

static metadata_desc_t* deq_metadata_descriptor()
{
	metadata_desc_t *pr = NULL;
	if (meta_q_lock(&metadata_queue, 2) == 0) {
		if (meta_q_wait_for_data(&metadata_queue) == 0) {
			pr = (metadata_desc_t *)meta_q_deq(&metadata_queue) ;
		}
		meta_q_unlock((void *)&metadata_queue, 2) ;
	}

	return pr;
}


void init_metadata_recording_queue()
{
	memset(&metadata_queue, 0, sizeof(meta_desc_q_t));

	memset(&all_metadata, 0, sizeof(metadata_rec_t));	

	meta_q_init(&metadata_queue);
}

void deinit_metadata_recording_queue()
{
    meta_q_deinit((void *)&metadata_queue);
}

int get_metadata_descriptor(metadata_desc_t *pdesc)
{
	int rc = 0;
	metadata_desc_t *tp = NULL;

	if (! meta_q_isempty(&metadata_queue))
	{
		tp = deq_metadata_descriptor();
		if (tp){
			memcpy(pdesc, tp, sizeof(metadata_desc_t));
			rc = 1;		
		}
	}

	return rc;
}

/*
 * set the current GPS
 *
 */
int set_gps(int32_t latitude, int32_t longitude, int32_t speed)
{
	metadata_desc_t  desc; 
	meta_gps_memory_t *ptr_gps = &(all_metadata.gps);

	memset(&desc, 0, sizeof(metadata_desc_t));
	memset(ptr_gps, 0, sizeof(meta_gps_memory_t));

	if (ptr_gps){		
		ptr_gps->latitude_dd_m = latitude;
		ptr_gps->longitude_dd_m = longitude;
		ptr_gps->speed_mph_m = speed;     

		desc.pData = (void *)ptr_gps;

		desc.data_type = eGPS;
		desc.data_length = sizeof(meta_gps_memory_t);
		
		gettimeofday(&(desc.tod), NULL) ;
		clock_gettime(CLOCK_MONOTONIC_RAW, &(desc.ts)) ;

		enq_metadata_descriptor(&desc);
	}
	
	return 0;
}

/*
 * set the time info
 *
 */
int set_time_info(uint32_t system_time, int64_t time_offset, char * time_source)
{
	metadata_desc_t  desc; 
	meta_time_memory_t *ptr_tm = &(all_metadata.timeinfo);

	memset(&desc, 0, sizeof(metadata_desc_t));
	memset(ptr_tm, 0, sizeof(meta_time_memory_t));

	if (ptr_tm){				
		ptr_tm->linux_time_sec_m = system_time;
		ptr_tm->timeoffset_sec_m = time_offset;
		strncpy(ptr_tm->time_src, time_source, (strlen(time_source)< TIME_SOURCE_NAME_LEN)?strlen(time_source):TIME_SOURCE_NAME_LEN);		
		
		desc.pData = (void *)ptr_tm;

		desc.data_type = eTimeInfo;
		desc.data_length = sizeof(meta_time_memory_t);
		
		gettimeofday(&(desc.tod), NULL) ;
		clock_gettime(CLOCK_MONOTONIC_RAW, &(desc.ts)) ;

		enq_metadata_descriptor(&desc);
	}
	
	return 0;
}

int set_locomotive_id(char * loco_id)
{
	metadata_desc_t  desc; 
	meta_loco_id_t *ptr_loco_id = &(all_metadata.locoId);
	int len = 0;

	memset(&desc, 0, sizeof(metadata_desc_t));
	memset(ptr_loco_id, 0, sizeof(meta_loco_id_t));
	

	if (ptr_loco_id)
	{	
		if (loco_id != NULL)
		{
			len = strlen(loco_id);
			syslog(LOG_INFO, "%s:%d: Loco ID %s len %d", __func__, __LINE__, loco_id, len);
			strncpy(ptr_loco_id->loco_id, loco_id, MAX_LOCO_ID_SIZE);			
			
			
			syslog(LOG_INFO, "%s: Loco ID received %s", __func__, ptr_loco_id->loco_id);
			
			desc.pData = (void *)ptr_loco_id;

			desc.data_type = eLocomotiveId;
			desc.data_length = sizeof(meta_loco_id_t);
			
			gettimeofday(&(desc.tod), NULL) ;
			clock_gettime(CLOCK_MONOTONIC_RAW, &(desc.ts)) ;

			enq_metadata_descriptor(&desc);

		}		
	}

	return 0;
}

/*
 * set the video event
 *
 */
int set_video_event(const char* digital_name, uint8_t digital_value)
{

	metadata_desc_t  desc; 
	meta_event_recording_t *ptr_evt = NULL;

	memset(&desc, 0, sizeof(metadata_desc_t));	

//  The video_rec sends the digital input configuration at startup. At that time the metarec has not 
//  started for any assigned cameras, or will not start since none of the cameras are assigned. Hence the
//  digital input configuration may not be available. Hence we do not have a reference for checking.
//  We have to trust that the event sent by video_rec are correct events and save them.

	ptr_evt = &(all_metadata.event[0]);		
	if (ptr_evt) {
		memset(ptr_evt, 0, sizeof(meta_event_recording_t));
		
		strncpy(ptr_evt->digital_name, digital_name, sizeof(ptr_evt->digital_name));
		ptr_evt->digital_name[sizeof(ptr_evt->digital_name) - 1] = '\0';
		ptr_evt->digital_value = digital_value;

		desc.pData = (void *)ptr_evt;

		desc.data_type = eVideoEvent;
		desc.data_length = sizeof(meta_event_recording_t);
		
		gettimeofday(&(desc.tod), NULL) ;
		clock_gettime(CLOCK_MONOTONIC_RAW, &(desc.ts)) ;
		
		syslog(LOG_NOTICE, "%s: Enqueue metadata descriptor: name: %s, value: %d", __func__, ptr_evt->digital_name, ptr_evt->digital_value);
		enq_metadata_descriptor(&desc);
	}			
	return 0;	
}

/*
 * add the digital config to the local mapping
 *
 */
int set_digital_input_config(const char* digital_name, int digital_number, bool digital_recording)
{
	int rc = 0;
	int idx = 0;

	digital_input_descriptor_t *ptr_digital_map_element = &(digital_events_config_map[0]);

	for( idx = 0; idx < NUM_DIGITAL_INPUTS; ++idx, ++ptr_digital_map_element)
    {
        if (ptr_digital_map_element->di_number == 0)
        {
        	strncpy(ptr_digital_map_element->di_name, digital_name, strlen(ptr_digital_map_element->di_name));
        	ptr_digital_map_element->di_number = digital_number;
        	ptr_digital_map_element->di_recording = digital_recording;
                    
        	rc = 1;
        	break;
        }
    }
    return rc;   
}

/*
 * get the digital config to the local mapping
 *
 */
int get_digital_input_name(int digital_number, char* digital_name)
{
	int rc = 0;
	int idx = 0;

	digital_input_descriptor_t *ptr_digital_map_element = &(digital_events_config_map[0]);

	for( idx = 0; idx < NUM_DIGITAL_INPUTS; ++idx, ++ptr_digital_map_element)
    {
        if (ptr_digital_map_element->di_number == digital_number)
        {
        	if (digital_name){
        		strncpy(digital_name, ptr_digital_map_element->di_name, strlen(digital_name));        	        
	        	rc = 1;
	        	break;	
        	}        	
        }
    }
    return rc;   
}

