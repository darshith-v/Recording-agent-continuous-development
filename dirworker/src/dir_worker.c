#include<stdio.h>
#include<string.h>
#include<stdint.h>
#include<stdlib.h>
#define __USE_XOPEN
#include<time.h>
#include<sys/types.h>
#include<string.h>
#include<pthread.h>
#include<fcntl.h>
#include<libgen.h>
#include<signal.h>
#include<unistd.h>
#include<stdbool.h>

#ifdef QNX_BUILD
#include <limits.h>
#else
#include <linux/limits.h>
#endif

#include "dir_walker.h"

#ifndef LOCOMOTIVE_ID_LENGTH
#define LOCOMOTIVE_ID_LENGTH 10
#endif

static void usage(char *cmd)
{
	char *base_name = basename(cmd) ;

	fprintf(stderr,"Usage:   %s -l loco-id [-d device] [-k track] [-s start-time] [-e end-time] [-a] [-q] [-r] [-i]\n", base_name);
	fprintf(stderr,"    -l -- locomotive-ID\n");
	fprintf(stderr,"    -d -- device, CHM or SSD\n");
	fprintf(stderr,"    -k -- track name after loco id, METADATA or [CAMERANAME.MAC] \n");
	fprintf(stderr,"    -s -- start time in YYYYMMDDTHHMMSS format\n");
	fprintf(stderr,"    -e -- end time in YYYYMMDDTHHMMSS format\n");	
	fprintf(stderr,"    -a -- list all files between given datetime range\n");
	fprintf(stderr,"    -q -- query, query file based on given start time\n");
	fprintf(stderr,"    -r -- remove, delete files that is earlier than the end time\n");
	fprintf(stderr,"    -i -- interval, get continous recording intervals\n");
}

/**
 * function: get_file_path to construct a file path based on filename  
 *
 * @param[in]: base_path  		- string of base search path
 * @param[in]: epoch_seconds  	- epoch seconds of file name
 *              
 * @return: directory path based on filename 
 */
static const char* get_file_path(const char* base_path, time_t epoch_seconds)
{
	struct tm *file_tm;
	static char file_path[PATH_MAX] = {0};

	file_tm = localtime(&epoch_seconds);

	// construct the full path /search base/YYYY/mm/dd/HH/file name
	snprintf(file_path, sizeof(file_path)-1, "%s/%04d/%02d/%02d/%02d", 
			 base_path, file_tm->tm_year+1900, file_tm->tm_mon+1, file_tm->tm_mday, file_tm->tm_hour);

	return file_path;
}


/**
 * function: parse_input_timestamp to parse the user input to epoch seconds  
 *
 * @param[in]: strtime  - string format timestamp
 *              
 * @return:  Epoch time in seconds 
 */
static unsigned long parse_input_timestamp(char* strtime)
{
	time_t time = 0; 

	struct tm tm;

	if (strtime[0]){		
		setenv("TZ", "GMT+0", 1);    

		memset(&tm, 0, sizeof(struct tm));

	    if (strptime(strtime,"%Y%m%dT%H%M%S",&tm) != NULL){
	    	time = mktime(&tm);  		
	    } 	    
	}

	return (unsigned long)time;
}

/**
 * function: get_timestamp_str to convert epoch seconds to human readable format  
 *
 * @param[in]: epochseconds  - time in epoch seconds
 *              
 * @return: string of date and time
 */
static const char* get_timestamp_str(unsigned long epochseconds)
{
	static char time_buffer[32] = {0};

	memset(&time_buffer, 0, sizeof(time_buffer));

	time_t rawtime = (time_t)epochseconds;

    struct tm  ts;

    localtime_r(&rawtime, &ts);

    strftime(time_buffer, sizeof(time_buffer)-1, "%Y%m%dT%H%M%S", &ts);

	return time_buffer;
}

int main(int argc, char *argv[])
{		
	int rc = EXIT_SUCCESS;
	char *base_name = NULL;
	int opt = 0;

	const char *twelve_underscores="____________" ;

	char search_dir_path[PATH_MAX]  = {0};

	char storage_location[128] 		= {0};			/* storage location */
	char loco_id[16] 				= {0};			/* loco id */
	char subpath[128] 				= {0};			/* subpath, METADATA or camera name and mac address */
	unsigned long start_time 		= 0;			/* start time in epoch seconds */
	unsigned long end_time 			= 0;			/* end time in epoch seconds */
	
	bool is_listall 				= false; 		/* list all files */
	bool is_searchfile 				= false; 		/* search for file */
	bool is_removefiles 			= false;		/* remove files */
	bool is_interval 				= false; 		/* recording intervals */

	// variables for serving request
	int i 							= 0;
	int count 						= 0; 
	
	chunk_file_t search_file;
	chunk_file_t files_array[MAX_NUMBER_FILES_PER_TRACK];	
	recording_interval_t recording_intervals[MAX_NUMBER_FILES_PER_TRACK];

	/* parse the command line arguments */
	while ((opt = getopt(argc, argv, ":d:l:k:s:e:aqri")) != -1) {
		switch (opt) {
			/* storgae location */
			case 'd':				
				if (strcmp(optarg,"SSD")==0){
					// copy the ssd location
					strcpy(&storage_location[0], "/mnt/removableSSD/video/VideoFootages");
				}
				else{
					// copy the chm location
					strcpy(&storage_location[0], "/mnt/chmSSD/video/VideoFootages");
				}
				break;										
			/* loco_ID */
			case 'l':
				// Loco ID will not be used in directory search path.
				memset(&loco_id[0], '\0', LOCOMOTIVE_ID_LENGTH) ;
				break;
			/* subpath after loco id */
			case 'k':
				// It was flagged that the variable 'n' was overwritten during while performing SAST.
				//Seemed to a redundant variable and hence removing it here and in the previous case-block.
				if (strlen(optarg) > 0) 
				{
					strncpy(&subpath[0], optarg, sizeof(subpath)-1);
				}
				else {
					strcpy(&subpath[0], "METADATA");
				}				
				break;
			/* start time */
			case 's':
				start_time = parse_input_timestamp(optarg);		
				break;
			/* end time */
			case 'e':
				end_time = parse_input_timestamp(optarg);
				break;			
			/* list all */
			case 'a':
				is_listall = true;				
				break;
			/* query */
			case 'q':
				is_searchfile = true;				
				break;		
			/* remove */
			case 'r':
				is_removefiles = true;				
				break ;
			/* interval */
			case 'i':
				is_interval = true;				
				break ;
			case ':':
				fprintf(stderr, "option needs a value\n");
				return 1 ;
				break;
			case '?':
				usage(argv[0]) ;
				return 1 ;
				break;
		}
	}

	if ((storage_location[0] == '\0')) {
		strcpy(&storage_location[0], "/mnt/chmSSD/video/VideoFootages");
	}

	if ((subpath[0] == '\0')) {
		strcpy(&subpath[0], "METADATA");
	}

	/* make sure no more than one option is selected */
	if ((is_listall && is_searchfile) 	  || 
		(is_listall && is_removefiles) 	  ||
		(is_listall && is_interval) 	  ||
		(is_searchfile && is_interval) 	  ||
		(is_searchfile && is_removefiles) ||
		(is_interval && is_removefiles)) {
		usage(argv[0]) ;
		exit(1) ;
	}

	// Dir search path does not include Loco ID.
	snprintf(search_dir_path, sizeof(search_dir_path) - 1, "%s/%s", storage_location, subpath);

	if ( search_dir_path[0] )
	{
		if (is_listall == true) {
			if (get_all_files(search_dir_path, start_time, end_time, files_array, &count) == EXIT_SUCCESS) {
				/* print the output file names */
				for (i = 0; i < count; ++i)
				{
					printf("%d: %s/%s\n", i+1, get_file_path(search_dir_path, files_array[i].epochseconds), files_array[i].filename);   
				}
			} 			               
		}

		if (is_searchfile == true) {
			if (start_time == 0) {
				printf("Searching a file needs a start_time !\n");
				usage(argv[0]) ;
			}
			else {
				if (get_file_on_time(search_dir_path, start_time, &search_file) == EXIT_SUCCESS) {
					printf("%s/%s\n", get_file_path(search_dir_path, search_file.epochseconds), search_file.filename);
				}	
			}			 
		}

		if (is_removefiles == true) {
			if (end_time == 0) {
				printf("Deleting files needs a end_time !\n");
				usage(argv[0]) ;
			}
			else {
				if (groom_files(search_dir_path, end_time, files_array, &count) == EXIT_SUCCESS) {
					/* print the output file names */
					for (i = 0; i < count; ++i)
					{
						printf("%d: %s/%s\n", i+1, get_file_path(search_dir_path, files_array[i].epochseconds), files_array[i].filename);   
					}
				}				
			}			 	
		}	

		if (is_interval == true) {
			if (get_all_recordings(search_dir_path, start_time, end_time, recording_intervals, &count) == EXIT_SUCCESS) {
				/* print the recording intervals */
				for (i = 0; i < count; ++i)
				{									
					printf("%d: ", i+1 );
					printf("%s", get_timestamp_str(recording_intervals[i].startTime)); 
					printf(" --> ");  
					printf("%s\n", get_timestamp_str(recording_intervals[i].endTime));
				}
			} 			               
		}	
	}

	exit(rc);
}
