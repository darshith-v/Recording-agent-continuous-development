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
#include <syslog.h>
#include <errno.h>

#include "metacollector.h"
#include "metawriter.h"

#ifndef LOCOMOTIVE_ID_LENGTH
#define LOCOMOTIVE_ID_LENGTH 10
#endif

int use_pipe = 0 ;

static void usage(char *cmd)
{
	char *base_name = basename(cmd) ;

	fprintf(stderr,"Usage:   %s -l loco-id [-d device] [-k track] [-s start-time] [-e end-time] [-g true/false] [-v true/false] [-t true/false] [-h] [-p]\n", base_name);
	fprintf(stderr,"    -l -- locomotive-ID\n");
	fprintf(stderr,"    -d -- device, CHM or SSD\n");
	fprintf(stderr,"    -k -- track name after loco id, METADATA or [CAMERANAME.MAC] \n");
	fprintf(stderr,"    -s -- start time in YYYYMMDDTHHMMSS format\n");
	fprintf(stderr,"    -e -- end time in YYYYMMDDTHHMMSS format\n");	
	fprintf(stderr,"    -g -- gps, true/false\n");
	fprintf(stderr,"    -v -- video event, true/false\n");
	fprintf(stderr,"    -t -- time change, true/false\n");	
	fprintf(stderr,"    -i -- Loco ID changes, true/false\n");		
	fprintf(stderr,"    -p -- use pipe format\n");	
	fprintf(stderr,"    -h -- validate hash\n");	
	fprintf(stderr,"    file path of input/output xml file\n");	
}

/**
 * function: parse_input_timestamp to parse the user input to epoch seconds  
 *
 *  @param[in]: strtime  - string format timestamp
 *              
 *  @return:  Epoch time in seconds 
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

int main(int argc, char *argv[])
{		
	int rc = EXIT_SUCCESS;
	char *base_name = NULL;
	int opt = 0, n = 0;

	const char *ten_underscores="__________" ;
	uid_t   process_id = 0, euid = 0;
    gid_t   group_id = 0;

	collector_input_chan_t chan = 
	{
		"",					/* storage location */
		"",					/* loco id */
		"METADATA",			/* subpath, METADATA or camera name and mac address */		
		0,					/* start time in epoch seconds */
		0, 					/* end time in epoch seconds */
		false,				/* whether gps retrival is enable or not */
		false,				/* whether video event retrival is enable or not */
		false,				/* whether time offset retrival is enable or not */
		false,				/* whether locoId retrival is enable or not */
		false,				/* flag for hash validation */
		"", 				/* input/output file path */
	};


	// Get current process ID
	process_id = getuid();
	euid = geteuid();
	// Get current effective group ID
	group_id = getegid();

	// Check if root
	if ( (euid == 0) && (group_id == 0))
        {
                        // When reducing the privileges, set the group ID first before setting the euid (effective user ID), otherwise group ID doesn't get changed.
                if (setegid((gid_t)  REAGENT_EFFECTIVE_GROUP_ID) == 0)  // Set effective group ID
                {
                        // Reduce privileges only if running as root.
                        if (seteuid((uid_t) REAGENT_EFFECTIVE_USER_ID) != 0)    // Set effective user ID
                        {
                                syslog(LOG_ERR, "%s: Unable to set current euid. Error %s", __func__, strerror(errno));
                                return EXIT_FAILURE;
                        }

                }
                else
                {
                        syslog(LOG_NOTICE, "%s: Unable to set current group ID. Error %s", __func__, strerror(errno));
                        return EXIT_FAILURE;
                }
        }	
	/* parse the command line arguments */
	while ((opt = getopt(argc, argv, ":d:l:k:s:e:t:g:i:v:hp")) != -1) {
		switch (opt) {
			/* storgae location */
			case 'd':				
				if (strcmp(optarg,"SSD")==0){
					// copy the ssd location
					strcpy(&chan.storage_location[0], "/mnt/removableSSD/video/VideoFootages/");
				}
				else{
					// copy the chm location
					strcpy(&chan.storage_location[0], "/mnt/chmSSD/video/VideoFootages/");
				}
				break;										
			/* loco_ID */
			case 'l':
				n = strlen(optarg);
				// It was flagged while performing SAST that chan.loco_id isn't null-terminated. Hence, this update.
				memset(chan.loco_id, '\0', sizeof(chan.loco_id));
				if (n >= LOCOMOTIVE_ID_LENGTH)
				{
					memcpy(&chan.loco_id[0], optarg, LOCOMOTIVE_ID_LENGTH);
				}
				else
				{
					memcpy(&chan.loco_id[0], ten_underscores, LOCOMOTIVE_ID_LENGTH);
					memcpy(&chan.loco_id[0], optarg, n);
				}
				break;
			/* subpath after loco id */
			case 'k':
				strncpy(&chan.subpath[0], optarg, sizeof(chan.subpath)-1);
				break;
			/* start time */
			case 's':
				chan.start_time = parse_input_timestamp(optarg);		
				break;
			/* end time */
			case 'e':
				chan.end_time = parse_input_timestamp(optarg);
				break;			
			/* gps */
			case 'g':
				if (strcmp(optarg,"true")==0){
					chan.gps_enabled = true;
				}
				break;
			/* video event */
			case 'v':
				if (strcmp(optarg,"true")==0){
					chan.event_enabled = true;
				}
				break;		
			/* time offset */
			case 't':
				if (strcmp(optarg,"true")==0){
					chan.timeoffset_enable = true;
				}
				break ;
			case 'i':
				if (strcmp(optarg,"true")==0){
					chan.locoId_enable = true;
				}
				break;
			case 'h':
					chan.validate_hash = true;
				break;
			case 'p':
				use_pipe = 1 ;
				break ;
			case ':':
				fprintf(stderr, "option needs a value\n");
				return 1 ;
				break;
			case '?':
				usage(argv[0]) ;
				return 1 ;
				break;
			default:
				fprintf(stderr, "Invalid options\n");				
				break;
		}
	}

	// get the file path
	if (optind < argc) {		
		strncpy(&chan.file[0], argv[optind], sizeof(chan.file) - 1);		 
	}

	/* make sure that loco_id, camera name and camera serial were specified. Just exit otherwise */
	if ((chan.loco_id[0]=='\0')) {
		usage(argv[0]) ;
		exit(1) ;
	}

	// write the document
	rc = write_metadata_xml_document(&chan);

	exit(rc);
}
