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

#include "metadata_def.h"
#include "metadata_util.h"
#include "xml_writer.h"

extern int use_pipe ;

// define for write to console or file, use printf if file is NULL 
#define PRINTF_T(str, file)    do { if (file) fprintf(file, "%s", str); else printf("%s", str);} while (0)


// static function prototypes 
static char* get_str_element_tag(const char* tagname, const char* tagvalue);
static char* get_int_element_tag(const char* tagname, int tagvalue);

static void write_gps(FILE *file, gps_record_t record);
static void write_timeoffset(FILE *file, timeoffset_record_t record);
static void write_video_event(FILE *file, video_event_record_t record);
static void write_locoid_event(FILE *file, locomotive_id_record_t record);

/**
 * function: get_str_element_tag to form/construct an element with tag name and integer tag value
 *
 * @param[in]: tagname 	- name of the tag 
 * @param[in]: tagvalue - value of the tag
 *              
 * @return:  string of this element as <tagname>tagvalue</tagname>
 */
static char* get_str_element_tag(const char* tagname, const char* tagvalue)
{
	static char buffer[255] = {0};

	memset(buffer, 0, sizeof(buffer));

	snprintf(buffer, sizeof(buffer)-1, "<%s>%s</%s>\n", tagname, tagvalue, tagname);

	return buffer;
}

/**
 * function: get_int_element_tag to form/construct an element with tag name and integer tag value
 *
 * @param[in]: tagname 	- name of the tag 
 * @param[in]: tagvalue - value of the tag
 *              
 * @return:  string of this element as <tagname>tagvalue</tagname>
 */
static char* get_int_element_tag(const char* tagname, int tagvalue)
{
	static char buffer[255] = {0};

	memset(buffer, 0, sizeof(buffer));

	snprintf(buffer, sizeof(buffer)-1, "<%s>%d</%s>\n", tagname, tagvalue, tagname);

	return buffer;
}

/**
 * function: write_gps to write gps complex tag
 *
 * @param[in]: file 	- pointer of an file 
 * @param[in]: record 	- gps record
 *              
 * @return:  void
 */
static void write_gps(FILE *file, gps_record_t record)
{
	char speed_string[GPS_SPEED_MAX_STRING_LENGTH + 1];
	char coordinate_string[GPS_COORDINATE_MAX_STRING_LENGTH + 1];
	char time_buffer[GPS_TIME_IN_SECS_MAX_STRING_LENGTH + 1];

	if (use_pipe)
	{
		// start of gps data
		PRINTF_T("<data>", file);

		PRINTF_T(time_seconds_tostr(record.record_time, &time_buffer[0]), file);
		PRINTF_T("|", file);
		PRINTF_T(gps_coordinate_tostr(record.data.latitude_dd_m, &coordinate_string[0], eGPSLatitude, false), file);
		PRINTF_T("|", file);
		PRINTF_T(gps_coordinate_tostr(record.data.longitude_dd_m, &coordinate_string[0], eGPSLongitude, false), file);
		PRINTF_T("|", file);
		PRINTF_T(gps_speed_tostr(record.data.speed_mph_m, &speed_string[0]), file);

		// close of gps data
		PRINTF_T("</data>\n", file);	
	}
	else
	{
		// start of gps tag
		PRINTF_T("<gps>\n", file);

		PRINTF_T(get_str_element_tag("time", time_seconds_tostr(record.record_time, &time_buffer[0])), file);
		PRINTF_T(get_str_element_tag("latitude", gps_coordinate_tostr(record.data.latitude_dd_m, &coordinate_string[0], eGPSLatitude, false)), file);
		PRINTF_T(get_str_element_tag("longitude", gps_coordinate_tostr(record.data.longitude_dd_m, &coordinate_string[0], eGPSLongitude, false)), file);
		PRINTF_T(get_str_element_tag("speed", gps_speed_tostr(record.data.speed_mph_m, &speed_string[0])), file);

		// close of gps tag
		PRINTF_T("</gps>\n", file);	
	}	
}

/**
 * function: write_timeoffset to write time change complex tag
 *
 * @param[in]: file 	- pointer of an file 
 * @param[in]: record 	- timeoffset record
 *              
 * @return:  void
 */
static void write_timeoffset(FILE *file, timeoffset_record_t record)
{
	char time_buffer[GPS_TIME_IN_SECS_MAX_STRING_LENGTH + 1];

	// start of time change tag
	PRINTF_T("<time_change>\n", file);

	PRINTF_T(get_str_element_tag("time", time_seconds_tostr(record.record_time, &time_buffer[0])), file);
	PRINTF_T(get_int_element_tag("current_offset", record.data.timeoffset_sec_m), file);
	PRINTF_T(get_str_element_tag("time_source", record.data.time_src), file);
	
	// close of time change tag
	PRINTF_T("</time_change>\n", file);
}

/**
 * function: write_video_event to write video event complex tag
 *
 * @param[in]: file 	- pointer of an file 
 * @param[in]: record 	- video event record
 *              
 * @return:  void
 */
static void write_video_event(FILE *file, video_event_record_t record)
{
	char time_buffer[GPS_TIME_IN_SECS_MAX_STRING_LENGTH + 1];

	// start of video event tag
	PRINTF_T("<event>\n", file);

	PRINTF_T(get_str_element_tag("time", time_seconds_tostr(record.record_time, &time_buffer[0])), file);
	PRINTF_T(get_str_element_tag("event_name", record.data.digital_name), file);
	PRINTF_T(get_int_element_tag("event_value", record.data.digital_value), file);
	
	// close of video event tag
	PRINTF_T("</event>\n", file);
}

/**
 * function: write loco ID to write locomotive ID event tag
 *
 * @param[in]: file 	- pointer of an file 
 * @param[in]: record 	- video event record
 *              
 * @return:  void
 */

static void write_locoid_event(FILE *file, locomotive_id_record_t record)
{
	char time_buffer[GPS_TIME_IN_SECS_MAX_STRING_LENGTH + 1];

	// start of video event tag
	PRINTF_T("<event>\n", file);

	PRINTF_T(get_str_element_tag("time", time_seconds_tostr(record.record_time, &time_buffer[0])), file);
	PRINTF_T(get_str_element_tag("locomotive_id", record.data.loco_id), file);
		
	// close of video event tag
	PRINTF_T("</event>\n", file);
}
/**
 * function: init_xml_document_declaration to write the declaration of an xml document
 *
 * @param[in]: file - pointer of an file 
 *              
 * @return:  EXIT_SUCCESS
 */
int init_xml_document_declaration(FILE *file)
{
	int rc = EXIT_SUCCESS; 
	char buffer[128] = {0};

	snprintf(buffer, sizeof(buffer)-1, "%s", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");

	PRINTF_T(buffer, file);	
	
	return rc;
}

/**
 * function: write_gps_list to write the gps metadata to console or xml document
 *
 * @param[in]: file              - pointer of file 
 * @param[in]: p_gps 			 - array/pointer of gps records
 * @param[in]: count 			 - number of records to write
 *              
 * @return:  EXIT_SUCCESS
 */
int write_gps_list(FILE *file, gps_record_t *p_gps, int count)
{
	int rc  = EXIT_SUCCESS;
	int idx = 0;		

	for (idx = 0; idx < count; ++idx)
	{
		write_gps(file, p_gps[idx]);			
	}

	return rc; 
}


/**
 * function: write_event_list to write the video event metadata to console or xml document
 *
 * @param[in]: file           	 - pointer of file 
 * @param[in]: p_event 			 - array/pointer of video event records
 * @param[in]: count 			 - number of records to write
 *              
 * @return:  EXIT_SUCCESS
 */
int write_event_list(FILE *file, video_event_record_t *p_event, int count)
{	
	int rc  = EXIT_SUCCESS;
	int idx = 0;

	for (idx = 0; idx < count; ++idx)
	{
		write_video_event(file, p_event[idx]);			
	}

	return rc; 
}

/**
 * function: write_timeoffset_list to write the time offset metadata to console or xml document
 *
 * @param[in]: file            	 - pointer of file 
 * @param[in]: p_timeoffset		 - array/pointer of time offset records
 * @param[in]: count 			 - number of records to write
 *              
 * @return:  EXIT_SUCCESS
 */
int write_timeoffset_list(FILE *file, timeoffset_record_t *p_timeoffset, int count)
{
	int rc  = EXIT_SUCCESS;
	int idx = 0;

	for (idx = 0; idx < count; ++idx)
	{
		write_timeoffset(file, p_timeoffset[idx]);			
	}

	return rc; 
}

/**
 * function: write loco ID  metadata to console or xml document
 *
 * @param[in]: file            	 - pointer of file 
 * @param[in]: p_locoid		 	- array/pointer of time loco ID records
 * @param[in]: count 			 - number of records to write
 *              
 * @return:  EXIT_SUCCESS
 */
int write_locoID_list(FILE *file, locomotive_id_record_t *p_locoid, int count)
{
	int rc  = EXIT_SUCCESS;
	int idx = 0;

	for (idx = 0; idx < count; ++idx)
	{
		write_locoid_event(file, p_locoid[idx]);
	}

	return rc; 
}


/**
 * function: write corrupt segments to console or xml document
 *
 * @param[in]: file            	 - pointer of file 
 * @param[in]: epochseconds		 	- start time of chunk in epoch format
 * @param[in]: duration_seconds 	- duration of chunk in seconds
 *              
 * @return:  EXIT_SUCCESS
 */
int write_corrupt_segment(FILE *file,unsigned long epochseconds,int duration_seconds )
{
	int  rc = EXIT_SUCCESS;

	char timeBuff[GPS_TIME_IN_SECS_MAX_STRING_LENGTH + 1];
	char timeBuffPlusDuration[GPS_TIME_IN_SECS_MAX_STRING_LENGTH + 1];
	// update metadata xml file
	PRINTF_T("<time_range>", file);
	PRINTF_T(get_str_element_tag("start_time", time_seconds_tostr(epochseconds, &timeBuff[0])), file);
	PRINTF_T(get_str_element_tag("end_time", time_seconds_tostr(epochseconds+duration_seconds, &timeBuffPlusDuration[0])), file);
	PRINTF_T("</time_range>", file);

	return rc; 
}




/**
 * function: write_list_start to write the start complex tag of the list based on type
 *
 * @param[in]: file            	 - pointer of file 
 * @param[in]: type		 		 - metadata type
 *              
 * @return:  N/A
 */
void write_list_start(FILE *file, eMetaDataType type)
{
	switch(type)
	{
		case eGPS:
			// start of gps list tag
			if (use_pipe) {
				PRINTF_T("<gpsdata>\n", file);
			} else {
				PRINTF_T("<gps_list>\n", file);	
			}	
			break;
		case eTimeInfo: 
			// start of time offset list tag
			PRINTF_T("<time_change_list>\n", file);
			break;
		case eVideoEvent:
			// start of video event list tag
			PRINTF_T("<video_events>\n", file);
			break;
		case eLocomotiveId:
			// start of locomotive ID list tag
			PRINTF_T("<locomotiveId_events>\n", file);
			break;
		case eCorruption:
			// start of locomotive ID list tag
			PRINTF_T("<corrupt_metadata_segments>\n", file);
			break;
		default:
			syslog(LOG_ERR, "%s: Invalid case mentioned %d", __func__, type);
			break;
	}
}


/**
 * function: write_list_start to write the close complex tag of the list based on type
 *
 * @param[in]: file            	 - pointer of file 
 * @param[in]: type		 		 - metadata type
 *              
 * @return:  N/A
 */
void write_list_close(FILE *file, eMetaDataType type)
{
	switch(type)
	{
		case eGPS:
			// close of gps list tag	
			if (use_pipe) {
				PRINTF_T("</gpsdata>\n", file);
			} else {
				PRINTF_T("</gps_list>\n", file);	
			}				
			break;
		case eTimeInfo: 
			// close of time offset list tag
			PRINTF_T("</time_change_list>\n", file);
			break;
		case eVideoEvent:
			// close of video event list tag
			PRINTF_T("</video_events>\n", file);
			break;
		case eLocomotiveId:
			// start of locomotive ID list tag
			PRINTF_T("</locomotiveId_events>\n", file);
			break;
		case eCorruption:
			// start of locomotive ID list tag
			PRINTF_T("</corrupt_metadata_segments>\n", file);
			break;
		default:
			syslog(LOG_ERR, "%s: Invalid case mentioned %d", __func__, type);
			break;
	}
}

