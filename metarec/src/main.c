#include<stdio.h>
#include<string.h>
#include<stdint.h>
#include<stdlib.h>
#include<time.h>
#include<sys/types.h>
#include<string.h>
#include<pthread.h>
#include <fcntl.h>
#include <libgen.h>
#include<signal.h>
#include<unistd.h>
#include <syslog.h>

#include "ptc_config.h"

#include "crc_utils.h"
#include "logger.h"
#include "ptc_api.h"
#include "video_api.h"

#include "ptc_meta_intf.h"
#include "path_util.h"
#include "metaintf.h"
#include "metaio.h"

#ifndef LOCOMOTIVE_ID_LENGTH
#define LOCOMOTIVE_ID_LENGTH 10
#endif

static volatile int keep_running = 1 ;
static int ptc_connected = 0 ;

int verbose = 0 ;

static void sig_handler(int signo)
{
	if ((signo == SIGINT) || (signo == SIGQUIT)) {
		if (verbose) {
			fprintf(stderr, "received %s\n", (signo == SIGINT) ? "SIGINT" : "SIGQUIT");
		}
		keep_running = 0;
	}
}

static void usage(char *cmd)
{
	char *base_name = basename(cmd) ;

	fprintf(stderr,"Usage:   %s -l loco-id [-1 chm-data-dir] [-2 rssd-data-dir] [-d chunk-duration] [-r recording mode] [-v]\n", base_name);
	fprintf(stderr,"    -l -- locomotive-ID\n");
	fprintf(stderr,"    -1 -- chm-data-dir, default /mnt/chmSSD/video/VideoFootages/\n");
	fprintf(stderr,"    -2 -- rssd-data-dir, default /mnt/removableSSD/video/VideoFootages/\n");
	fprintf(stderr,"    -r -- recording mode\n");
	fprintf(stderr,"    -v -- be verbose\n");
	fprintf(stderr,"    -d -- chunk-duration, number of records. default 120\n\n");
}

static void metadata_io_chan_init(metadata_io_chan *pchan)
{
	int i, n ;
	if (pchan) {
		char tmp[MAXPATH/2] ;

		for (i=0; i<MAXDATALOC; i++) {
			strcpy(tmp, pchan->data_loc[i].basedir) ;
			n = strlen(tmp) ;
			if ((n > 0) && (tmp[--n] == '/')) {
				tmp[n] = '\0' ;
			}
			n = sprintf(pchan->data_loc[i].datadir, "%s/%s/", tmp, "METADATA") ;	
			strncpy(pchan->data_loc[i].stagingdir, pchan->data_loc[i].datadir, n) ;
			strcpy(&(pchan->data_loc[i].stagingdir[n]), "staging/") ;			
		}
	}
}

int main(int argc, char *argv[])
{	
	char name[] = "metarec";
		
	int rc = EXIT_SUCCESS;
	char *base_name = NULL;
	int opt = 0, n = 0;
	char *locoId = NULL;
	uint32_t systime = 0;
	int64_t offset = 0;
	char *time_src = NULL;
	char time_src_name[TIME_SOURCE_NAME_LEN]="";
	char *time_str = NULL;
	char *get_env = NULL;


	const char *ten_underscores="__________" ;

	metadata_io_chan chan = {
		"",					/* loco_ID */
		0,					/* ia_active */
		(pthread_t)0,		/* worker_thread */		
		0,					/* number of data storage areas */
		/* storage_area: disp_name, is_used, base_dir, data_dir */
		{
			{"CHM", 0, "/mnt/chmSSD/video/VideoFootages/", "", ""},
			{ "RSSD", 0, "/mnt/removableSSD/video/VideoFootages/", "", ""}
		},
		"",					/* sub directory path */
		120,				/* metadata chunk size, number of records */
		0x03, 				/* recording mode */		
	};
	
	/* parse the command line arguments */
	while ((opt = getopt(argc, argv, ":r:d:l:1:2:v")) != -1) {
		switch (opt) {
			/* recording mode */
			case 'r':
				n = 0 ;
				if (sscanf(optarg,"%x",&n) > 0) {
					chan.recording_mode = n & 0x1f ;
				}
				break;
			/* chunk size */
			case 'd':
				n = 0 ;
				n=atoi(optarg) ;
				if (n > 0) {
					chan.chunk_duration = n ;
				}
				break;
			/* loco_ID */
			case 'l':
				n = strlen(optarg) ;
				// Loco ID is not used.
				memset(&chan.src_premise[0], '\0', sizeof(chan.src_premise));
				strncpy(&chan.src_premise[0], optarg, LOCOMOTIVE_ID_LENGTH) ;
				
				break;
			/* CHM data dir */
			case '1':
				strncpy(&chan.data_loc[0].basedir[0], optarg, sizeof(chan.data_loc[0].basedir)-1) ;
				chan.n_copies += 1;
				break;
			/* RSSD data dir */
			case '2':
				strncpy(&chan.data_loc[1].basedir[0], optarg, sizeof(chan.data_loc[1].basedir)-1) ;
				chan.n_copies += 1;
				break;			
			case 'v':
				verbose = 1 ;
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

	/* make sure that loco_id, camera name and camera serial were specified. Just exit otherwise */
	if ((chan.src_premise[0]=='\0')) {
		usage(argv[0]) ;
		exit(1) ;
	}

	/* update the drive if being used according to the recording mode */
	chan.data_loc[0].is_used = IS_CHM_METADATA_RECORDING_COMMANDED(chan.recording_mode) ;
	chan.data_loc[1].is_used = IS_SSD_METADATA_RECORDING_COMMANDED(chan.recording_mode) ; 

	/* initialize the PTC client here and attach the interested messages */
    ptc_connected = ptci_up_meta(argc, argv, name) ;
	if (ptc_connected) {
		/* make sure ptc intf has the commanded recording mode */
		ptci_set_rm_meta(chan.recording_mode) ; 
	}

	if (signal(SIGINT, sig_handler) == SIG_ERR) {
	  fprintf(stderr, "\ncan't catch SIGINT\n");
	}
	if (signal(SIGQUIT, sig_handler) == SIG_ERR) {
	  fprintf(stderr, "\ncan't catch SIGQUIT\n");
	}

	metadata_io_chan_init(&chan);

	rc = metadata_recording_init(&chan) ;


	if (rc == EXIT_SUCCESS) {	
		// This sleep is necessary for the thread and queue to initialize properly. Otherwise,
		// the metadata is not saved (sometimes). Just wondering, whether we could miss any PTC
		// message for this sleep. 
		sleep(1);

		// Retrieve the Time Info from the environment variable and save it to metarec at startup.
		get_env = getenv("TIME_INFO");
		if (get_env != NULL)
		{
			// Timestamp
			time_str = strstr(get_env, "timestamp=");
			if (time_str != NULL)
			{
				time_str += strlen("timestamp=");
				if (time_str != NULL)
				{
					systime = atoi(time_str);
					syslog(LOG_INFO, "%s:%d: Systime retrieved from environment variable %d", __func__, __LINE__, systime);	
				}

				// Time offset
				time_str = strstr(get_env, "time_offset=");
				if (time_str != NULL)
				{
					time_str += strlen("time_offset=");
					if (time_str != NULL)
					{
						offset = (long long) atol(time_str);
						syslog(LOG_INFO, "%s:%d: Time offset retrieved from environment variable %ld", __func__, __LINE__, offset);	
					}

					// Time source
					time_str = strstr(get_env, "src='");	
					if (time_str != NULL)
					{
						time_str += strlen("src='");
						strncpy(time_src_name,time_str,strlen(time_str));
						*strchr(time_src_name, '\'')='\0';
						time_src = time_src_name ;
						syslog(LOG_INFO, "%s:%d: Time source retrieved from environment variable %s", __func__, __LINE__, time_src);

						// Save this in metadata
						set_time_info((uint32_t)  systime, (int64_t) offset, (char *) time_src);
					}
				}
			}
		}

		// Once the meta data is initialized enter the startup loco ID.
		set_locomotive_id(&chan.src_premise[0]);		
		
		do {	
			if ((ptc_connected) && (ptc_should_exit() == FALSE)){				
				ptc_poll();	 // process attached PTC messages
			}							
			
			usleep(300000); // take a 300 millseconds nap
		}while ((keep_running) && (chan.is_active)) ;

		metadata_recording_stop(&chan) ;
	}
	else {
		fprintf(stderr,"*** metadata_writer_init() failed!\n") ;
	}
	if (ptc_connected) {
		ptc_stop();	
	}
	
	return rc ;
}
