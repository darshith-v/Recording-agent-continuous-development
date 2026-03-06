#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>

#include "metadata_def.h"
#include "metachunk.h"
#include "metareader.h"

// #define _DEBUG

// static variables 
static rcd_idx_t max_index_records[METADATA_CHUNK_MAX_INDEX_RECORDS]; 


// static functions 
static int get_all_filtered_index_records(FILE          *fp, 
										  unsigned long  starttime, 
						  				  unsigned long  endtime, 
						  		 		  eMetaDataType  type, 
						  		 		  rcd_idx_t     *pindex_rcd, 
						  		 		  int           *rcd_cnt);

/**
 * function: get_all_filtered_index_records to read the file for metadata index records;
 * Based on the type it will filter out un-matching and only keep the matching index records;
 *
 * @param[in]: fp            - pointer of an open file
 * @param[in]: starttime 	 - start time in epoch seconds
 * @param[in]: endtime 		 - end time in epoch seconds
 * @param[in]: type 		 - metadata type, gps, time, or event
 * @param[out]: pindex_rcd   - pointer of allocated array of metadata index record
 * @param[out]: rcd_cnt 	 - number of matching metadata index records
 *              
 *  @return:  EXIT_SUCCESS/EXIT_FAILURE
 */
static int get_all_filtered_index_records(FILE          *fp, 
										  unsigned long  starttime, 
						  				  unsigned long  endtime, 
						  		 		  eMetaDataType  type, 
						  		 		  rcd_idx_t     *pindex_rcd, 
						  		 		  int           *rcd_cnt)
{
	int rc = EXIT_FAILURE;

	int    max_idx_records_cnt = 0; 
	int    idx 			       = 0;
	int    matched_idx_records = 0;
	size_t numItemsRead        = 0;

	rcd_idx_t  index_rcd;
	rcd_idx_t *p_idx = &index_rcd;

	if (fp != NULL)
	{
		// calculate the max number of idx records 
		max_idx_records_cnt = (sizeof(metachunk_file_pfx_t) - sizeof(struct subpfx_2) - sizeof(struct subpfx_1))/sizeof(rcd_idx_t);

		// loop reading for idx records 
		for (idx = 0; idx < max_idx_records_cnt; ++idx)
		{
			// reset the index record
			memset(p_idx, 0, sizeof(rcd_idx_t));			

			// seek to next index record
			if (fseek(fp, sizeof(struct subpfx_2) + sizeof(struct subpfx_1) + (sizeof(rcd_idx_t) * idx), SEEK_SET) != 0)
			{
				syslog(LOG_ERR, "%s: Unable to seek to the next index record.  fseek() returned: %d", __func__, errno);
			}
			else
			{
				// do another reading 
				numItemsRead = fread(p_idx, sizeof(rcd_idx_t), 1, fp);

				if (numItemsRead != 1)
				{
					syslog(LOG_ERR, "%s: Unable to read [%d] type Data from metadata raw records.  fread() returned: %ld", __func__, type, numItemsRead);
				}
				else
				{
					// 'Dereference before null check' flagged by SAST is now fixed.
					if (p_idx->timestamp != 0 && p_idx->byteoffset != 0)
					{
#ifdef _SHOW_RECORD
						printf("%s#%d: record timestamp: %d; offset: %d; record type: %d; record length: %d\n",
						__func__, __LINE__,	p_idx->timestamp, p_idx->byteoffset, p_idx->recordtype, p_idx->recordlength );	
#endif
						// memcpy to the pointers and update rcd cnt
						if ( (p_idx->recordtype == type) &&  		// make sure the type matches
							 ((starttime == 0 && endtime == 0) ||   // when start time && end time are 0, which implies ALL
							 ((starttime != 0 && p_idx->timestamp >= starttime) && (endtime != 0 && p_idx->timestamp <= endtime)))
						   )
						{
							memcpy(pindex_rcd, p_idx, sizeof(rcd_idx_t));
							pindex_rcd++;
							matched_idx_records += 1;
						}				
					} // if (p_idx && p_idx->timestamp != 0 && p_idx->byteoffset != 0)
					else
					{
						break; // reached invalid index/ non-initialized index
					}
				} // if (numItemsRead == 1)
			}
		} // for (idx = 0; idx < max_idx_records_cnt; ++idx)
		// update the records count 
		*rcd_cnt = matched_idx_records;
		rc = EXIT_SUCCESS; 
	} // if (fp != NULL) 
#ifdef _DEBUG
		printf("%s#%d: matched_idx_records %d \n", __func__, __LINE__, matched_idx_records );
#endif			
	return rc ;
} // get_all_filtered_index_records

/**
 * function: get_digital_config_name to read digital config name;
 *
 * @param[in]: fp            - pointer of an open file
 * @param[in]: dig_num 	 	 - digital configuration number
 *              
 *  @return:  EXIT_SUCCESS/EXIT_FAILURE
 */
const char *get_digital_config_name(FILE *fp, int dig_num)
{
	static char digital_name[EVENT_NAME_LENGTH + 1] = {0};

	memset(&digital_name, 0, sizeof(digital_name));

	int    idx          = 0;
	size_t numItemsRead = 0;

	struct subpfx_1        pfx1; 
	struct digital_config* ptr_config = NULL;

	if (fp != NULL)
	{
		// make sure read from 0
		if (fseek(fp, 0, SEEK_SET) != 0)
		{
			syslog(LOG_ERR, "%s: Unable to seek to the zero index record.  fseek() returned: %d", __func__, errno);
		}
		else
		{
			// read the subpfx1  that includs digital mappings 
			numItemsRead = fread(&pfx1, sizeof(struct subpfx_1), 1, fp);

			if (numItemsRead != 1)
			{
				syslog(LOG_ERR, "%s: Unable to read Digital Input Configuration Number.  fread() returned: %ld", __func__, numItemsRead);
			}
			else
			{
				ptr_config = &pfx1.digital_mappings[0];

				for (idx = 0; idx < NUM_DIGITAL_INPUTS; ++idx, ++ptr_config)
				{
					// found it
					if (ptr_config->dig_num == dig_num)
					{
						strncpy(&digital_name[0], &(ptr_config->dig_name[0]), EVENT_NAME_LENGTH);
						break;
					}
				} // for (idx = 0; idx < NUM_DIGITAL_INPUTS; ++idx, ++ptr_config)
			} // if (numItemsRead == 1)
		} // if (fseek(fp, 0, SEEK_SET) == 0)
	} // if (fp != NULL)
	return digital_name;
} // get_digital_config_name


/**
 * function: get_metadata_records to read the file for metadata raw records;
 * This function takes void pointer as input for generic convenience, the caller is responsiable for
 * casting and un-casting to the expected structure pointer for data access
 *
 * @param[in]: fp            - pointer of an open file
 * @param[in]: starttime 	 - start time in epoch seconds
 * @param[in]: endtime 		 - end time in epoch seconds
 * @param[in]: type 		 - metadata type, gps, time, or event
 * @param[out]: ptr_rcd  	 - pointer of allocated array of metadata records
 * @param[out]: rcd_cnt 	 - number of matching metadata records
 *              
 *  @return:  EXIT_SUCCESS/EXIT_FAILURE
 */
int get_metadata_records(FILE          *fp, 				
						 unsigned long  starttime, 
						 unsigned long  endtime, 
						 eMetaDataType  type, 
						 void          *ptr_rcd, 
						 int           *rcd_cnt)
{
	int    rc              = EXIT_FAILURE; 
	int    idx             = 0;
	int    matched_records = 0;
	int    offset          = 0;
	int    length          = 0;
	size_t numItemsRead    = 0;

	gps_record_t           gps_record; 
	timeoffset_record_t    time_record;
	video_event_record_t   event_record; 
	locomotive_id_record_t locoId;
	
	rcd_idx_t *ptr = &max_index_records[0];

	memset(ptr, 0, sizeof(rcd_idx_t)*METADATA_CHUNK_MAX_INDEX_RECORDS);

	if (fp != NULL)
	{
		rc = get_all_filtered_index_records(fp, starttime, endtime, type, ptr, &matched_records);
#ifdef _DEBUG
		printf("%s#%d: Number of matched index records %d for type: %d\n", __func__, __LINE__, matched_records, type );
#endif							
		if (rc == EXIT_SUCCESS)
		{
			ptr = &max_index_records[0];

			for ( idx = 0; idx < matched_records; ++idx, ++ptr)
			{
				if (ptr)
				{							
					offset = ptr->byteoffset; 
					length = ptr->recordlength;

					// read the file for the raw record
					// Return of fseek wasn't checked initially, therefore SAST flagged this. Hence, the check is in place now.
					if(fseek(fp, offset, SEEK_SET) < 0)
					{
						syslog(LOG_ERR, "%s: Unable to read the file for the raw record.\n", __func__);
						return EXIT_FAILURE;
					}

					switch(type)
					{
						case eGPS: // read gps
							memset(&gps_record, 0, sizeof(gps_record_t));
							gps_record.record_time = ptr->timestamp;
							numItemsRead = (fread(&(gps_record.data), length, 1, fp));

							if (numItemsRead != 1)
							{
								syslog(LOG_ERR, "%s: Unable to read GPS Data from metadata raw records.  fread() returned: %ld", __func__, numItemsRead);
							}
							else
							{
								memcpy(ptr_rcd, &gps_record, sizeof(gps_record_t));
								ptr_rcd += sizeof(gps_record_t);
							}							
							break;
						case eTimeInfo: // read time info	
							memset(&time_record, 0, sizeof(timeoffset_record_t));					
							time_record.record_time = ptr->timestamp;
							numItemsRead = (fread(&(time_record.data), length, 1, fp));

							if (numItemsRead != 1)
							{
								syslog(LOG_ERR, "%s: Unable to read Time Info from metadata raw records.  fread() returned: %ld", __func__, numItemsRead);
							}
							else
							{
								memcpy(ptr_rcd, &time_record, sizeof(timeoffset_record_t));
								ptr_rcd += sizeof(timeoffset_record_t);
							}
							break;
						case eVideoEvent: // read video event	
							memset(&event_record, 0, sizeof(video_event_record_t));					
							event_record.record_time = ptr->timestamp;
							numItemsRead = (fread(&(event_record.data), length, 1, fp));

							if (numItemsRead != 1)
							{
								syslog(LOG_ERR, "%s: Unable to read Video Event from metadata raw records.  fread() returned: %ld", __func__, numItemsRead);
							}
							else
							{
								memcpy(ptr_rcd, &event_record, sizeof(video_event_record_t));
								ptr_rcd += sizeof(video_event_record_t);
							}
							break;
						case eLocomotiveId:
							memset(&locoId, 0, sizeof(locomotive_id_record_t));
							locoId.record_time = ptr->timestamp;
							numItemsRead = (fread(&(locoId.data), length, 1, fp));

							if (numItemsRead != 1)
							{
								syslog(LOG_ERR, "%s: Unable to read Locomotive ID from metadata raw records.  fread() returned: %ld", __func__, numItemsRead);
							}
							else
							{
								memcpy(ptr_rcd, &locoId, sizeof(locomotive_id_record_t));
								ptr_rcd += sizeof(locomotive_id_record_t);
							}
							break;
						default:
							syslog(LOG_ERR, "%s: Invalid case mentioned %d", __func__, type);
							break;
					} // switch(type)
				} // if (ptr)
			} // for ( idx = 0; idx < matched_records; ++idx, ++ptr)
			*rcd_cnt = matched_records;	
			rc = EXIT_SUCCESS;
		} // if (rc == EXIT_SUCCESS)		
	} // if (fp != NULL)
	return rc; 
} // get_metadata_records
