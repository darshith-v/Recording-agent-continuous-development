/**
 * @file    main.c
 *
 * @author  WRE SW Engineer
 *
 * @section DESCRIPTION
 *
 * Main thread for Reagent
 *
 * @section COPYRIGHT
 *
 * Copyright 2021 WABTEC Railway Electronics
 * This program is the property of WRE.
 */

#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef QNX_BUILD
#include <signal.h>
#else
#include <sys/signal.h>
#endif
#include <stdarg.h>
#include <libgen.h>
#include <syslog.h>

#include "reagent_config.h"
#include "video_api.h"
#include "ptc_intf.h"
#include "http.h"
#include "user_config.h"
#include "domutil.h"

#include "ptc_config.h"

#include "logger.h"
#include "ptc_api.h"
#include "camon.h"
#include "ovconfig.h"

#include "groom_util.h"
#include "video_api.h"

// Effective user ID and group ID. TODO: Can also read from the files
#define REAGENT_EFFECTIVE_USER_ID	5555	// should match /etc/passwd file (user reagent)
#define REAGENT_EFFECTIVE_GROUP_ID	5555	// should match /etc/group file (user reagent)

int verbose;

static volatile int ptc_active = 0;

static int   org_argc;
static char *org_argv[8];

extern int stop_recording_process(media_info_t *p_media,camdef_t *p_camdef);

enum
{
	eConfigNotAvailable = 0,
	eConfigAvailable    = 1
};

// Struct for saving the camera name and pid.
typedef struct
{
    char         name[CAMERA_NAME_LENGTH + 4];	// Camera length is defined as 12.
	pid_t        worker_pid;
	unsigned int recording_mode_flags;
	unsigned int recording_status_flags;
} camera_pid_t;

typedef struct
{
	camera_pid_t camera_pids[MAX_CAMS];
} restore_camera_pid_t;

// Handle signal
void handle_signal(int signal)
{
    // Find out which signal we're handling
    switch (signal)
    {
        case SIGTERM:
        case SIGINT:
        case SIGQUIT:
        {
            fprintf(stderr, "Caught Termination Signal, leaving the event loop / exiting now\n");
			syslog(LOG_INFO, "Caught Termination Signal, leaving the event loop / exiting now\n") ;

            if (ptc_active)
            {
            	ptc_active = 0;
            	ptc_client_stop();
            }
            http_stop(signal);

            //exit(0);
            break;
        }
        case SIGHUP:		/* do nothing on sighup for now */
        {
        	int                   n_recorders                    = 0;
        	media_info_t         *p_media                        = raconf_get_media_info(0);
        	int                   old_rdy[2];
			restore_camera_pid_t  restore_camera_pid;
			unsigned int          recording_mode_flags[MAX_CAMS] = {0};
			pid_t                 metarec_pid, spacemon_pid;
			camdef_t             *p_camdef                       = NULL;
    		int                   i,j                            = 0;
			meta_recorder_t      *pmeta_rec                      = NULL;
			space_mon_t          *p_space_mon                    = NULL;
			int                   n_cams_recording               = 0;

			syslog(LOG_INFO, "SIGHUP caught -- re-reading camera assignment configuration ... \n") ;

			memset(&restore_camera_pid, 0, sizeof(restore_camera_pid));

			if (p_media != NULL)
			{
				old_rdy[0] = p_media->runtime.is_chm_ready;
				old_rdy[1] = p_media->runtime.is_rssd_ready;

				// Save the camera recording process pids if already running.
				for (j = 0; j < MAX_CAMS; j++) 
				{
					p_camdef = &p_media->cameras[j];

					if (p_camdef != NULL)
					{
						memcpy(restore_camera_pid.camera_pids[j].name, p_camdef->name, sizeof(restore_camera_pid.camera_pids[j].name));
						restore_camera_pid.camera_pids[j].worker_pid = p_camdef->worker_pid;
						// Save run time status flags.
						restore_camera_pid.camera_pids[j].recording_mode_flags = p_camdef->runtime.recording_mode_flags;
						restore_camera_pid.camera_pids[j].recording_status_flags = p_camdef->runtime.recording_status_flags;						
						syslog(LOG_INFO, "%s: Save camera %s and pid %d", __func__, restore_camera_pid.camera_pids[j].name, restore_camera_pid.camera_pids[j].worker_pid);						
					}
				}
				
				// Metarec pid
				pmeta_rec = &(p_media->metaworker);

				if (pmeta_rec != NULL)
				{
					metarec_pid = pmeta_rec->worker_pid;
					syslog(LOG_INFO, "%s: Save existing  metarec pid %d", __func__, metarec_pid);					
				}
			
				// Save space mon pid
				p_space_mon = &(p_media->spacemonitor);

				if (p_space_mon != NULL)
				{
					spacemon_pid = p_space_mon->worker_pid;
					syslog(LOG_INFO, "%s: Save existing  space monitor pid %d", __func__, spacemon_pid);					
				}

                n_cams_recording = p_media->runtime.n_cams_recording;
				syslog(LOG_INFO, "%s: Save num cameras currently recording %d", __func__, n_cams_recording);									
			} // end if (p_media != NULL)

        	n_recorders = raconf_init(org_argc, org_argv);
			syslog(LOG_INFO, "%s,%d: num recorders %d", __func__, __LINE__, n_recorders);

			// Restore saved parameters.
			if (p_media != NULL)
			{
				p_media->runtime.is_rssd_ready = old_rdy[1] ;
				p_media->runtime.is_chm_ready = old_rdy[0] ;

				// Restore num cameras currently recording.
                p_media->runtime.n_cams_recording = n_cams_recording;

				// Metarec pid
				pmeta_rec = &(p_media->metaworker);

				if (pmeta_rec != NULL)
				{
					pmeta_rec->worker_pid = metarec_pid;
					syslog(LOG_INFO, "%s: Restore existing  metarec pid %d", __func__, pmeta_rec->worker_pid);					
				}
			
				// space mon pid
				p_space_mon = &(p_media->spacemonitor);

				if (p_space_mon != NULL)
				{
					p_space_mon->worker_pid = spacemon_pid;
					syslog(LOG_INFO, "%s: Restore existing  space monitor pid %d", __func__, p_space_mon->worker_pid);	
				}

				// Restore the camera recording process pids if already running.
				for (i = 0; i < MAX_CAMS; i++) 
				{
					p_camdef = &p_media->cameras[i];

					if (p_camdef != NULL)
					{
						if (p_camdef->name[0] != '\0')
						{
							// Find the corresponding camera
							for (j = 0; j < MAX_CAMS; j++)
							{
								if (strncmp(p_camdef->name, restore_camera_pid.camera_pids[j].name, strlen(p_camdef->name)) == 0)
								{
									// Restore pids only if the camera is assigned. If not assigned, terminate all running processes.
									p_camdef->worker_pid = restore_camera_pid.camera_pids[j].worker_pid;								
									syslog(LOG_INFO, "%s: Restore camera %s, assigned %d, pid %d", __func__, p_camdef->name, p_camdef->runtime.is_assigned, p_camdef->worker_pid);

									if (p_camdef->runtime.is_assigned)
									{
										syslog(LOG_INFO, "%s: Restore camera %s and pid %d", __func__, p_camdef->name, p_camdef->worker_pid);
										
										// Restore status flags.
										p_camdef->runtime.recording_mode_flags   = restore_camera_pid.camera_pids[j].recording_mode_flags;
										p_camdef->runtime.recording_status_flags = restore_camera_pid.camera_pids[j].recording_status_flags;

										// Restore on-line status if Camera is still Recording
										if ((p_camdef->runtime.recording_status_flags &
											(IS_RECORDING_CHM_AUDIO | IS_RECORDING_SSD_AUDIO | IS_RECORDING_CHM_VIDEO | IS_RECORDING_SSD_VIDEO)) != 0)
										{
											p_camdef->runtime.is_online = 1;
										}
									}
									else
									{
										// If camera is not assigned and worker pid is non-zero
										if (p_camdef->worker_pid > 0)
										{
											// Terminate recording process if already running.
											stop_recording_process(p_media,p_camdef);
										}
									}
									break;
								}
							} // end for (j = 0; j < MAX_CAMS; j++)
							send_camera_status(p_camdef);
						} // end if (p_camdef->name[0] != '\0')
					}	  // end if (p_camdef != NULL)
				}		  // end for (i=0; i < MAX_CAMS; i++)
				syslog(LOG_INFO, "%s: Restore num cameras currently recording %d", __func__, p_media->runtime.n_cams_recording);
			}			  // end if (p_media != NULL)

        	if (n_recorders > 0)
    		{
    			raconf_show();
    		}
    		else
            {
    			fprintf(stderr, "\n*** %s configuration cannot be re-read or parsed\n", org_argv[1]);

                if (ptc_active)
                {
                	ptc_active = 0;
                	ptc_client_stop();
                }
                http_stop(SIGINT);
   		    }

            // need to rescan the drives for footage
            syslog(LOG_INFO, "%s: Rescan drives for footage", __func__);

#ifndef QNX_BUILD
            if (initialize_recordings(NULL) != EXIT_SUCCESS)
            {
                syslog(LOG_INFO, "%s: Rescan failed", __func__);
            }
            else
            {
                syslog(LOG_INFO, "%s: Rescan succeeded", __func__);
            }
#endif
        	break ;
        }
        default:
        {
			syslog(LOG_INFO, "%s,%d: Unknown signal.", __func__, __LINE__);			
        	break;
        }
    } // end switch (signal)
} // handle_signal

static void usage(char **argv)
{
	fprintf(stderr, "\nUsage:\n\t%s <xml-file> ...\n\n", basename(argv[0]));
}

int main(int argc, char **argv)
{
	int      rc                     = 0;
	int      n_recorders            = 0;
	int      videoAppStatus         = eConfigNotAvailable;
	int      cameraIndex            = 0;
	uint8_t  numCamSettingsReceived = 0;
	char    *recorder_sn            = getenv("RECORDER_SN") ;
	uid_t	process_id, euid;
	gid_t  	group_id;

	// Get current process ID
	process_id = getuid();
	euid = geteuid();
	// Get current effective group ID
	group_id = getegid();

#ifdef DEBUG
	syslog(LOG_NOTICE, "%s[%d]: Current process ID: %d, euid %d, current group ID: %d", __func__, __LINE__, process_id, euid, group_id);
#endif

	// Check if root
	if ( (euid == 0) && (group_id == 0))
	{
			// When reducing the privileges, set the group ID first before setting the euid (effective user ID), otherwise group ID doesn't get changed.
       		if (setegid((gid_t)  REAGENT_EFFECTIVE_GROUP_ID) == 0)	// Set effective group ID
			{
				group_id = getegid();
				syslog(LOG_NOTICE, "%s: Set current group ID to %d", __func__, REAGENT_EFFECTIVE_GROUP_ID);
				
				// Reduce privileges only if running as root.
				if (seteuid((uid_t) REAGENT_EFFECTIVE_USER_ID) == 0)	// Set effective user ID
				{
					euid = geteuid();
					syslog(LOG_NOTICE, "%s: Set current euid to %d", __func__, euid);
				}
				else
				{
					syslog(LOG_ERR, "%s: Unable to set current euid. Error %s", __func__, strerror(errno));
				}
			}
			else
			{
				syslog(LOG_NOTICE, "%s: Unable to set current group ID. Error %s", __func__, strerror(errno));
			}
	}

	syslog(LOG_NOTICE, "%s: Current process ID: %d, euid %d, current group ID: %d", __func__, process_id, euid, group_id);

#if 0
// For testing.
#ifdef DEBUG
	sleep(1);
	// When elevating privileges, increase the effective user ID first and then the group ID.
	if (seteuid((uid_t) 0) == 0) // Set effective user ID
	{
		// Get current process ID
		process_id = getuid();
		euid = geteuid();
		syslog(LOG_NOTICE, "%s: Set current euid to %d", __func__, euid);
		if (setegid((gid_t)  0) == 0) // Set effective group ID
		{
			// Get current effective group ID
			group_id = getegid();
			syslog(LOG_NOTICE, "%s: Set current group ID to %d", __func__, group_id);
		}
		else
		{
			syslog(LOG_NOTICE, "%s: Unable to set current group ID. Error %s", __func__, strerror(errno));
		}
	}
	else
	{
		syslog(LOG_ERR, "%s: Unable to set current euid. Error %s", __func__, strerror(errno));
	}

	syslog(LOG_NOTICE, "%s: Current process ID: %d, euid %d, current group ID: %d", __func__, process_id, euid, group_id);
#endif
#endif

	if (argc > 1)
	{
		org_argc = argc ;
		for (org_argc=0; org_argc < argc;  org_argc++ ) {
			if (org_argc < 8) {
				org_argv[org_argc] = argv[org_argc] ;
			}
			else {
				break ;
			}
		}

		/* collect user config information */
		uc_collect() ;

		if (uc_is_ready()) {
			syslog(LOG_INFO, "User Configuration info was fully collected -- accepting\n") ;
			du_accept_user_config(REAGENT_BASE_CONFIG_FILE, recorder_sn, uc_get_locoid(), uc_get_camera_list()) ;
		}

        if (!((strcmp(REAGENT_APP, argv[0]) == 0) && (strcmp(REAGENT_CURRENT_CONFIG_FILE, argv[1]) == 0)))
		{
			syslog(LOG_ERR, "%s: Incorrect arguments!\n", __func__);
		}

		n_recorders = raconf_init(argc, argv);

		if (n_recorders > 0)
		{
			raconf_show();
			
#ifdef __aarch64__ 
            // unsure of what this is for, keep structure the same as before
            create_shared_mem();
			set_drive_availability(1 /*e_rssd*/,(uint8_t)1); 
            initialize_shared_mem();
#else
#ifndef QNX_BUILD
            create_and_initialize_shared_mem();     // shared mem is needed even if no cameras are attached/assigned/etc.
#endif
#endif

			rc = ptc_client_init(1, argv);

			if (rc == 0)
			{
				void *xtra_state ;

				ptc_active = 1;

				signal(SIGTERM, handle_signal);
				signal(SIGINT, handle_signal);
				signal(SIGQUIT, handle_signal);
				signal(SIGHUP, handle_signal);

				syslog(LOG_INFO, "starting HTTP service in a backround and entering the main loop\n") ;

				/* shift to the next provided command line parameter, if any, before starting the HTTP server */
				argc    -= 1;
				argv[1]  = argv[0];
				++argv;
				xtra_state = http_start(argc, argv);

				/* enter PTC event loop */
				ptc_client_run(xtra_state);

				if (ptc_active)
				{
					ptc_active = 0;
					ptc_client_stop();
					sleep(1);
					printf("\n*** PTC client left the event loop, exiting ...\n");
					syslog(LOG_INFO, "*** PTC client left the event loop, exiting\n") ;
				}
			} // end if (rc == 0)
			else
			{
				printf("ptc_client_init()returned %d, cannot proceed\n", rc);
				syslog(LOG_ERR, "ptc_client_init()returned %d, cannot proceed\n", rc) ;
			}
		} // end if (n_recorders > 0)
		else
		{
			fprintf(stderr, "\n*** Configuration cannot be read or parsed\n");
			syslog(LOG_ERR, "*** Configuration cannot be read or parsed\n");
			usage(argv);
			rc = 1;
		}
    } // end if (argc > 1)
    else
    {
    	usage(argv);
    	rc = 1;
    }
	return (rc);
} // main

// Note: This is probably not the place to put this function. But putting it in user-config.c 
// forces changes inclusion of domutil.o to other applications like recording_list or access_data.
// Hence this function is put here since it also doesn't belong in ptc_intf.c (originally it was there).
//
/**
 * Function to update the config files with new Loco ID information
  *
 * @param[in] Loco ID
 *
 * @return    None
 */
void update_config_files(char *locoid)
{
    mxml_node_t *node = NULL;

    // Update Base config file.
    node = get_config_file((char *) REAGENT_BASE_CONFIG_FILE);

    if (node != NULL)
    {
        // Update Loco ID
        du_set_child_new_data((mxml_node_t *) node, (char *) "loco_id", (char *) locoid) ;
        save_config_file((char *) REAGENT_BASE_CONFIG_FILE, (mxml_node_t *) node);
    }

    // Update current config file.
    node = get_config_file((char *) REAGENT_CURRENT_CONFIG_FILE);

    if (node != NULL)
    {
        // Update Loco ID
        du_set_child_new_data((mxml_node_t *) node, (char *) "loco_id", (char *) locoid) ;
        save_config_file((char *) REAGENT_CURRENT_CONFIG_FILE, (mxml_node_t *) node);
    }
    return;
}
#ifdef QNX_BUILD
#include <stdarg.h>
void __va_copy(va_list d, va_list s)
{
    va_copy(d, s) ;
}
#endif
