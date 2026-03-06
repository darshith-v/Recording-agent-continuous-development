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

#include "dir_walker.h"
#include "metareader.h"
#include "xml_writer.h"

#include "metawriter.h"
#include "metacoc.h"

//#define _DEBUG
#define SMALL_SIZE_BUFF  256
#define MID_SIZE_BUFF    512
#define LARGE_SIZE_BUFF  1024

// static variables
static gps_record_t gps_data[METADATA_CHUNK_MAX_INDEX_RECORDS];
static video_event_record_t event_data[METADATA_CHUNK_MAX_INDEX_RECORDS];
static timeoffset_record_t time_data[METADATA_CHUNK_MAX_INDEX_RECORDS];
static locomotive_id_record_t locoid_data[METADATA_CHUNK_MAX_INDEX_RECORDS];

// static variables for file searching via dir_walker
static int files_count = 0;
static char search_base_dir[SMALL_SIZE_BUFF] = {0};
static chunk_file_t found_files[MAX_NUMBER_FILES_PER_TRACK];

// static function prototypes
static void search_and_write_metadata(FILE* mfile, unsigned long start_time, unsigned long end_time, eMetaDataType type);

extern int write_locoID_list(FILE *file, locomotive_id_record_t *p_locoid, int count);

/**
 * function: search_and_write_metadata to read through all the files and search for intested metadata type
 * once records are obtained, write to console or file 
 *
 * @param[in]: mfile            - file pointer, can be NULL
 * @param[in]: start_time 		- requested start time in epoch seconds
 * @param[in]: end_time 		- requested end time in epoch seconds
 * @param[in]: type 			- metadata type
 *              
 * @return:  EXIT_SUCCESS/EXIT_FAILURE
 */
static void validate_and_write_metadata(FILE* mfile)
{
	int idx 			= 0; 
	int is_valid = -1;

	time_t file_epoch_secs  = 0;
	struct tm *file_tm;
	FILE *fp 				= NULL;

	char filepath[LARGE_SIZE_BUFF] 	  = {0}; 

	// loop all files
	if (mfile == NULL)
		return;
	// start of metadata corruption tag
	write_list_start(mfile, eCorruption); 
	for (idx = 0; idx < files_count; ++idx)
	{
	 	// get the timestamp of file 
	 	file_epoch_secs = (time_t)found_files[idx].epochseconds;
	 	file_tm = localtime(&file_epoch_secs);

		// construct the full path /search base/YYYY/mm/dd/HH/file name
		snprintf(filepath, sizeof(filepath)-1, "%s/%04d/%02d/%02d/%02d/%s", 
				 search_base_dir, file_tm->tm_year+1900, file_tm->tm_mon+1, file_tm->tm_mday, file_tm->tm_hour, found_files[idx].filename);
#ifdef _DEBUG
		printf("%s#%d: Reading file %s\n", __func__, __LINE__, filepath );
#endif	
		is_valid = -1;
		validate_metadata_chunk(filepath,&is_valid);
		if (is_valid != 1)
		{
#ifdef _DEBUG
			printf("%s#%d: Corrupt chunk %s\n", __func__, __LINE__, filepath );
#endif	
			write_corrupt_segment(mfile,found_files[idx].epochseconds,found_files[idx].duration_seconds);
		}
	}
	write_list_close(mfile, eCorruption);  
}

/**
 * function: search_and_write_metadata to read through all the files and search for intested metadata type
 * once records are obtained, write to console or file 
 *
 * @param[in]: mfile            - file pointer, can be NULL
 * @param[in]: start_time 		- requested start time in epoch seconds
 * @param[in]: end_time 		- requested end time in epoch seconds
 * @param[in]: type 			- metadata type
 *              
 * @return:  EXIT_SUCCESS/EXIT_FAILURE
 */
static void search_and_write_metadata(FILE* mfile, unsigned long start_time, unsigned long end_time, eMetaDataType type)
{
	int idx 			= 0; 	
	int records_found 	= 0;

	time_t file_epoch_secs  = 0;
	struct tm *file_tm;
	FILE *fp 				= NULL;

	gps_record_t           *ptr_gps    = &gps_data[0];
	video_event_record_t   *ptr_event  = &event_data[0];
	timeoffset_record_t    *ptr_time   = &time_data[0];
	locomotive_id_record_t *ptr_locoid = &locoid_data[0];

	char filepath[MID_SIZE_BUFF] = {0};

	// loop all files
	for (idx = 0; idx < files_count; ++idx)
	{
		if(!strstr(found_files[idx].filename,".data"))
			continue;
	 	// get the timestamp of file 
	 	file_epoch_secs = (time_t)found_files[idx].epochseconds;
	 	file_tm = localtime(&file_epoch_secs);

		// construct the full path /search base/YYYY/mm/dd/HH/file name
		snprintf(filepath, sizeof(filepath)-1, "%s/%04d/%02d/%02d/%02d/%s", 
				 search_base_dir, file_tm->tm_year+1900, file_tm->tm_mon+1, file_tm->tm_mday, file_tm->tm_hour, found_files[idx].filename);

#ifdef _DEBUG
		printf("%s#%d: Reading file %s\n", __func__, __LINE__, filepath );
#endif	
	 	// open file 
		fp = fopen(filepath, "r");
		if (fp)
		{
			/* Search and Write metadata record */
			switch(type)
			{
				case eGPS:
					ptr_gps = &gps_data[0];
					memset(ptr_gps, 0, sizeof(gps_data));						
					// read data
					if (get_metadata_records(fp, start_time, end_time, eGPS, (void *)ptr_gps, &records_found) == EXIT_SUCCESS)
					{
#ifdef _DEBUG
						printf("%s#%d: Number of gps records %d\n", __func__, __LINE__, records_found );
#endif								
						if (records_found > 0){
							// write data	
							write_gps_list(mfile, gps_data, records_found);
						}							
					}
					break;
				case eVideoEvent:
					ptr_event = &event_data[0];
					memset(ptr_event, 0, sizeof(event_data));
					// read data
					if (get_metadata_records(fp, start_time, end_time, eVideoEvent, (void *)ptr_event, &records_found) == EXIT_SUCCESS)
					{
#ifdef _DEBUG
						printf("%s#%d: Number of video event records %d\n", __func__, __LINE__, records_found );
#endif															
						if (records_found > 0){
							// write data	
							write_event_list(mfile, event_data, records_found);
						}							
					}
					break;
				case eTimeInfo: 
					ptr_time = &time_data[0];
					memset(ptr_time, 0, sizeof(time_data));
					// read data
					if (get_metadata_records(fp, start_time, end_time, eTimeInfo, (void *)ptr_time, &records_found) == EXIT_SUCCESS)
					{
#ifdef _DEBUG
						printf("%s#%d: Number of time offset records %d\n", __func__, __LINE__, records_found );
#endif																			
						if (records_found > 0){
							// write data								
							write_timeoffset_list(mfile, time_data, records_found);
						}							
					}
					break;
				case eLocomotiveId:
					ptr_locoid = &locoid_data[0];
					memset(ptr_locoid, 0, sizeof(locoid_data));
					// read data
					if (get_metadata_records(fp, start_time, end_time, eLocomotiveId, (void *)ptr_locoid, &records_found) == EXIT_SUCCESS)
					{

						syslog(LOG_INFO, "%s#%d: Number of locomotive ID records %d", __func__, __LINE__, records_found );
						if (records_found > 0){
							// write data								
							write_locoID_list(mfile, locoid_data, records_found);
						}							
					}

					break;
				
				default:
					syslog(LOG_ERR, "%s: Invalid case mentioned %d", __func__, type);
					break;
			} // switch(type)
			
			// close file
			fclose (fp); 
		} // if (fp)
		else{
#ifdef _DEBUG		
			fprintf(stderr, "\nError opend file\n");
#endif							 
		}
	} // for (idx = 0; idx < files_count; ++idx)
} // search_and_write_metadata

/**
 * function: write_metadata_xml_document to write metadata xml document
 * based on the requsted info, which includes where, when, what
 *
 * @param[in]: pchan            - collector structure contains all requested info
 *              
 * @return:  EXIT_SUCCESS/EXIT_FAILURE
 */
int write_metadata_xml_document(collector_input_chan_t *pchan)
{
	int rc = EXIT_FAILURE; 

	static FILE *xmlfile = NULL; 

	if (pchan != NULL)
	{
		// make sure we have a file path
		if ( pchan->file[0] )
		{
			if (access(pchan->file, F_OK) != -1){
				// file already exist, open with append+
				xmlfile = fopen(pchan->file, "a+");
			}
			else{
				// file not exist, open with write+ 
				xmlfile = fopen(pchan->file, "w+");
				// add the xml declaration
				init_xml_document_declaration(xmlfile);
			}
		} // if ( pchan->file[0] )
		else 
		{
			// add the xml declaration
			init_xml_document_declaration(xmlfile);
		}
		
		/* construct the search base directory /mount point/METADATA */
		snprintf(search_base_dir, sizeof(search_base_dir)-1, "%s/%s", pchan->storage_location, "METADATA");
#ifdef _DEBUG
		printf("%s#%d: Searching files from %s\n", __func__, __LINE__, search_base_dir );
#endif	

		/* reset local varilables for clean search result */
		files_count = 0;
		memset(found_files, 0, sizeof(found_files));

		/* search for files and strore result as local static variables */	
		if (get_all_files(search_base_dir, pchan->start_time, pchan->end_time, found_files, &files_count) == EXIT_SUCCESS)
		{
#ifdef _DEBUG
			printf("%s#%d: Number of files found %d\n", __func__, __LINE__, files_count );			
#endif		
			if (pchan->validate_hash == true) 
			{
				/*Validate all found chunks and write metadata*/
				validate_and_write_metadata(xmlfile);		
			}

			/* check what metadata type needs to be searched and written */
			if (pchan->gps_enabled == true) // gps
			{
				write_list_start(xmlfile, eGPS); // start gps list
				search_and_write_metadata(xmlfile, pchan->start_time, pchan->end_time, eGPS);
				write_list_close(xmlfile, eGPS); // close gps list
			}

			if (pchan->event_enabled == true) // video event
			{
				write_list_start(xmlfile, eVideoEvent); // start video event list
				search_and_write_metadata(xmlfile, pchan->start_time, pchan->end_time, eVideoEvent);
				write_list_close(xmlfile, eVideoEvent); // close video event list
			}

			if (pchan->timeoffset_enable == true) // time offset
			{
				write_list_start(xmlfile, eTimeInfo); // start timeoffset list
				search_and_write_metadata(xmlfile, pchan->start_time, pchan->end_time, eTimeInfo);
				write_list_close(xmlfile, eTimeInfo); // close timeoffset list
			}

			if (pchan->locoId_enable == true)	// Loco Id
			{
				write_list_start(xmlfile, eLocomotiveId); // start timeoffset list
				search_and_write_metadata(xmlfile, pchan->start_time, pchan->end_time, eLocomotiveId);
				write_list_close(xmlfile, eLocomotiveId); // close timeoffset list
			}
		} // if (get_all_files(search_base_dir, pchan->start_time, pchan->end_time, found_files, &files_count) == EXIT_SUCCESS)

		/* make sure xml file is closed if open */
		if (xmlfile){
			fclose(xmlfile);	
		}	
		rc = EXIT_SUCCESS;
	} // if (pchan != NULL)

	return rc;
} // write_metadata_xml_document