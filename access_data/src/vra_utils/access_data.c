/**
 * @file    camera_recordings.c
 *
 * @author  rshafiq
 *
 * @section DESCRIPTION
 *
 * 
 * @section COPYRIGHT
 *
 * Copyright 2020 WABTEC Railway Electronics
 * This program is the property of WRE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <syslog.h>


#ifdef VDOCK

#include "vdock_util.h"

#define DEBUG 0
    #define syslog(priority, ...)  \
        do { if (DEBUG) printf(__VA_ARGS__); } while (0)
#else
#include <syslog.h>
#include "reagent_config.h"
#endif

#include "ra_data_search.h"
#include "ra_download.h"

#define TEMP_SESSION_ID_LENGTH              10
#define EPOCH_START_YEAR                    1900
#define EPOCH_START_MONTH                   1

#define RTSP_PLAYBACK                       "rtspplay"

#define LENGTH_TIMESTAMP		   32

int ra_verbose;

char vdock_video_dir[512] = "./VideoFootages";
#ifdef VDOCK
vdock_t vd;
#endif

enum
{
    e_start = 0,
    e_download,
    e_playback,
    e_end
};

enum
{
    e_chm = 1,
    e_rssd
};

int type = -1 , device = -1, m_type = -1;
char *p_start_time  = NULL, *p_end_time = NULL, *p_camera_name = NULL, *p_loco_id = NULL, *p_destination = NULL, *p_session_id = NULL ; 

void print_help()
{
    fprintf(stderr, "access_data -t <type> -s <start_time> -e <end_time> -c <camera_name> -l <loco_id> -d <destination>\n");
}


int run_rtspplay()
{
    int rc = system("/opt/reagent/bin/rtspplay 1>/dev/null 2>/dev/null &");
    rc = system("echo  ");

    return 0;
}

/**
 * function: get_timestamp_str to convert epoch seconds to human readable format  
 *
 * @param[in]: epochseconds  - time in epoch seconds
 *              
 * @return: void
 */
void get_timestamp_str(uint32_t epochseconds, char * time_buffer,size_t len)
{
    time_t rawtime = (time_t)epochseconds;

    struct tm  ts;
#ifndef VDOCK
    setenv("TZ", "GMT+0", 1);
    localtime_r(&rawtime, &ts);
#else
    ts = *localtime(&rawtime);
#endif

    strftime(time_buffer, len, "%Y%m%dT%H%M%S", &ts);
}


pid_t proc_find(const char* name) 
{
    DIR* dir;
    struct dirent* ent;
    char buf[512];

    long  pid;
    char pname[100] = {0,};
    char state;
    FILE *fp=NULL; 

    if (!(dir = opendir("/proc"))) {
        perror("can't open /proc");
        return -1;
    }

    while((ent = readdir(dir)) != NULL) {
        long lpid = atol(ent->d_name);
        if(lpid < 0)
            continue;
        snprintf(buf, sizeof(buf), "/proc/%ld/stat", lpid);
        fp = fopen(buf, "r");

        if (fp) {
            if ( (fscanf(fp, "%ld (%[^)]) %c", &pid, pname, &state)) != 3 ){
                printf("fscanf failed \n");
                fclose(fp);
                closedir(dir);
                return -1; 
            }
            if (!strcmp(pname, name)) {
                fclose(fp);
                closedir(dir);
                return (pid_t)lpid;
            }
            fclose(fp);
        }
    }


closedir(dir);
return -1;
}


int genID()
{
    int val = 0;
    FILE * fp = fopen("/tmp/pbC", "r") ;
    if (!fp) {
        fp = fopen("/tmp/pbC", "w") ;
        if (!fp) return -1 ; // fail
        fprintf(fp, "%d", 1) ;
        fclose(fp) ;
        return 1;
    }
    int rc = fscanf(fp, "%d", &val) ;
    val++;

    fclose(fp);                  // close file for read
    //Return of fopen wasn't checked initially, therefore SAST flagged this. Hence, the check is in place now.
    if ((fp = fopen("/tmp/pbC", "w")) != NULL) // reopen for write
    {
        fprintf(fp, "%d", val);
        fclose(fp);
    }
    return val;
}

/**
 * Function to create a epoch time stamp from file name
 * later on this epoch could be use as filter to find the 
 * desired video data.
 *
 * @param[in] file_name - file name string to convert 
 * @param[out] epoch    - epoch time stamp created from file name
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int to_epoch(const char *file_name, long int *epoch)
{
    //TODO is there better way to do it?

    struct tm t;
    time_t    t_of_day;
    int       v_date      = 0;
    const int const_10000 = 10000;
    const int const_100   = 100;
    const int index_time  = 9;


    // parse the file name and put the values in time struct
    // then covert it to epoch time
    v_date     = atoi(file_name);
    t.tm_year  = v_date / const_10000 - EPOCH_START_YEAR;
    t.tm_mon   = (v_date % const_10000) / const_100 - EPOCH_START_MONTH;
    t.tm_mday  = v_date % const_100;
    v_date     = atoi(file_name + index_time);
    t.tm_hour  = v_date/const_10000;
    t.tm_min   = (v_date % const_10000) / const_100;
    t.tm_sec   = v_date % const_100;
    t.tm_isdst = -1;        // Is DST on? 1 = yes, 0 = no, -1 = unknown
    t_of_day   = mktime(&t);
#ifdef DEBUG    
    syslog(LOG_INFO, "%s[%d]: year = %d, Month = %d, Day = %d, Hour = %d, Min = %d, Sec = %d, seconds since epoch %ld", 
        __func__,__LINE__,(int)t.tm_year, (int)t.tm_mon, (int)t.tm_mday, (int)t.tm_hour,(int)t.tm_min,(int)t.tm_sec, t_of_day);
#endif        
    *epoch = (long int)t_of_day;

    return (EXIT_SUCCESS);
}



int parse_arg(int argc, char* const *argv)
{
    int opt;

    while ((opt = getopt (argc, argv, "vt:i:m:s:e:c:l:d:")) != -1)
        switch (opt)
        {
            case 'v':
                ra_verbose = true;
                break;
            case 't':
                if (atoi(optarg) >= e_end || atoi(optarg) <= e_start)
                {
                    print_help();
                    return -1;
                }
                type = atoi(optarg);
                break;
            case 'i':
                p_session_id = optarg;
                break;
            case 'm':
                m_type = atoi(optarg);
                break;
            case 's':
                p_start_time = optarg;
                break;
            case 'e':
                p_end_time = optarg;
                break;
            case 'c':
                p_camera_name = optarg;
                break;
            case 'l':
                p_loco_id = optarg;
                break;
            case 'd':
                p_destination = (optarg);
                break;
            case ':':
                if (optopt != 'v')
                {
                    printf("\"-%c\" option needs a value\n",optopt);
                    return -1;
                }
            case '?': //used for some unknown options
                if (optopt != 'v')
                {
                    printf("unknown option: %c\n", optopt);
                    return -1;
                }   
            default:
                break;
        }

    if (type == -1 || p_start_time == NULL || p_end_time == NULL || p_camera_name == NULL || (p_destination == NULL && type == 1))
    {
        fprintf(stderr, "Invalid Arguments\n" );
        print_help();
        return -1;
    }
    return 0;
}

int main(int argc, char const *argv[])
{
    char *p_temp = NULL;
    long v_start_time = 0,v_end_time = 0,v_current_time = 0;
    va_download_t downloadMessage = {0};
    data_range_t range = {0};
    uint64_t file_size = 0;
    uint8_t status = 0;
    struct stat sb = {0};
    char ** array = NULL;
    ra_verbose = false;
    char start[LENGTH_TIMESTAMP]={0};
    char end[LENGTH_TIMESTAMP]={0};
    char tmp[CAMERA_NAME_LENGTH + 4] = {0};
    int i = 0, n = 0;
    uid_t	process_id = 0, euid = 0;
    gid_t  	group_id = 0;

    // Get current process ID
	process_id = getuid();
	euid = geteuid();
	// Get current effective group ID
	group_id = getegid();

	if ( (euid == 0) && (group_id == 0))
	{
			// When reducing the privileges, set the group ID first before setting the euid (effective user ID), otherwise group ID doesn't get changed.
       		if (setegid((gid_t)  REAGENT_EFFECTIVE_GROUP_ID) == 0)	// Set effective group ID
		{
			// Reduce privileges only if running as root.
			if (seteuid((uid_t) REAGENT_EFFECTIVE_USER_ID) != 0)	// Set effective user ID
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


    memset(&range,0,sizeof(data_range_t));

    array = malloc(2* sizeof(char *));
    if(array == NULL)
    {	
		syslog(LOG_ERR, "%s: Unable to allocate dynamic memory\n", __func__);
    	return EXIT_FAILURE;
    }
    for (i = 0; i< 2; i++)
    {
		array[i] = malloc(256*sizeof(char));
		if(array[i] == NULL)
		{
			if(i == 1)
				free(array[0]);					
			free(array);
			syslog(LOG_ERR, "%s: Unable to allocate dynamic memory\n", __func__);
			return EXIT_FAILURE;
		}
    }

    strcpy(array[0],"access_data");
    strcpy(array[1],"/opt/reagent/config/current_config.xml");
    
#ifndef VDOCK 
    media_info_t *p_minfo;
    int n_recorders = raconf_parse(2,array);
    p_minfo = raconf_get_media_info(0);
#endif
    for (i = 0; i< 2; i++)
        free(array[i]); 
    free(array);
#ifndef VDOCK 
    if (n_recorders <= 0 || p_minfo->n_cameras <= 0)
    {
        fprintf(stderr, "Error: 515 can't get/parse current_config\n");
        syslog(LOG_ERR, "%s: Error: 515 can't get/parse current_config", __func__);        
        return EXIT_FAILURE;
    }
#endif
   
    if (-1 == parse_arg(argc,(char* const *)argv))
    {
        fprintf(stderr, "Error: 404 invalid argument(s)\n" );
        syslog(LOG_ERR, "%s: Error: 404 invalid argument(s)", __func__);                
        return EXIT_FAILURE;
    }
#ifdef VDOCK
    memset(&vd,0,sizeof(vdock_t));
    if (EXIT_SUCCESS != init_vdock(&vd))
    {
        printf("*** Failed to init vdock\n");
        return(EXIT_FAILURE);
    }
#endif
    //fprintf(stderr, "%d %s %s %s %s %s\n",type, p_start_time, p_end_time, p_camera_name,p_loco_id, p_destination );


    // Convert start time and end time to epoch format
    p_temp = strchr(p_start_time,'T');
    *p_temp = '-';
    to_epoch(p_start_time,&v_start_time);
    p_temp = strchr(p_end_time,'T');
    *p_temp = '-';
    to_epoch(p_end_time,&v_end_time);
    v_current_time = time(NULL);

#ifdef DEBUG
    syslog(LOG_INFO, "%s: start_time requested %ld, end_time requested %ld, current time %ld", __func__, v_start_time, v_end_time, v_current_time);
#endif    

    if (m_type == -1)
    {
        m_type = e_chm; //default drive is chm
#ifdef DEBUG
        syslog(LOG_INFO, "%s[%d]: Invalid drive specified. Use default drive %s", __func__, __LINE__, (m_type == e_chm)?"CHM":"SSD");
#endif    

    }

    memset((char *) &downloadMessage, 0, sizeof(downloadMessage));
    memset(tmp, 0, sizeof(tmp));
    strncpy(tmp, p_camera_name, CAMERA_NAME_LENGTH);  
    n = strlen(tmp);
    for (i = n; i < CAMERA_NAME_LENGTH ; i++)
    {
        // Pad camera name with '_'
        tmp[i] = '_';
    }

    strncpy(downloadMessage.payload.camera, tmp, CAMERA_NAME_LENGTH);
    syslog(LOG_NOTICE, "%s: Download requested for camera %.12s", __func__, downloadMessage.payload.camera);

    downloadMessage.payload.start_time = v_start_time;
    downloadMessage.payload.end_time = v_end_time;

    if (type == e_playback ) //Instant Playback
    {        
        char temp_session_id[TEMP_SESSION_ID_LENGTH + 1] = {0};
        if (p_session_id == NULL)
        {            
            snprintf(temp_session_id, sizeof(temp_session_id) - 1, "%u", genID());
            p_session_id = temp_session_id;
        }
        char root_dir[80] = {'\0'};
        sprintf(root_dir,"/tmp/playback/%s",p_session_id);
        strcpy(downloadMessage.payload.destination,root_dir);

        // Check if RTSP server is running or not?
        if ( -1 == proc_find(RTSP_PLAYBACK))
        {

            if (ra_verbose)
              fprintf(stderr, "Server is not Running\nStarting RTSP server....\n");
            //RTSP Server is not running let start it
            int rc = run_rtspplay();
            syslog(LOG_NOTICE, "%s: Starting RTSP play.", __func__);                            
        }
        else
        {
            if (ra_verbose)
              fprintf(stderr, "RTSP Server is Running...\n");
        }

    }
    else if (type == e_download)
    {
        char temp_dest[1024] = {'\0'};
        strncpy(temp_dest, p_destination, 1023);
        char *p_temp = strrchr(temp_dest, '/');
        if (p_temp != NULL)
            *p_temp = '\0';

        if (stat(temp_dest, &sb) != 0 || !S_ISDIR(sb.st_mode) || strstr(p_destination,".lvdat") == NULL)
        {
            fprintf(stdout, "Error: 404 invalid destination\n" );
            syslog(LOG_ERR, "%s: Error: 404 invalid destination.", __func__);                            
            return EXIT_FAILURE;
        }


        if ( (v_start_time >= v_end_time) || (v_start_time > v_current_time))
        {
            fprintf(stdout, "Error: 416 invalid duration\n");
            syslog(LOG_ERR, "%s: Error: 416 invalid duration.", __func__);                                        
            return EXIT_FAILURE;
        }

        strncpy(downloadMessage.payload.destination, p_destination, DESTINATION_LENGTH);
        downloadMessage.payload.destination[DESTINATION_LENGTH - 1] = '\0';
#ifdef DEBUG
        syslog(LOG_INFO, "%s: download destination %s", __func__, downloadMessage.payload.destination);
#endif
    }



    if (EXIT_FAILURE == perform_download(&downloadMessage, type, m_type, &file_size, &status, &range))
    {
        switch(status)
        {
            case DOWNLOAD_RANGE_ERR:
                fprintf(stdout, "Error: 416 Video outside of Requested Range\n");
                syslog(LOG_ERR, "%s: Error: 416 Video outside of Requested Range", __func__);                
                break;
            case DOWNLOAD_OUT_OF_RANGE:
                fprintf(stdout, "Error: 404 No Video in the Requested Range\n");
                syslog(LOG_ERR, "%s: Error: 404 No Video in the Requested Range", __func__);                                
                break;
            case DOWNLOAD_FAILURE:
                fprintf(stdout, "Error: 404 Cannot Serve File\n");
                syslog(LOG_ERR, "%s: 404 Cannot Serve File.", __func__);                                
                break; 
            case DOWNLOAD_OK_PARTIAL:
            case DOWNLOAD_OK_FULL:
                fprintf(stdout, "Download Success\n");
                syslog(LOG_INFO, "%s: Download Success.", __func__);                                                
                break;
            case DOWNLOAD_SIZE_ERR:
                fprintf(stdout, "Error: 413 Video Requested Too Large\n");
                syslog(LOG_ERR, "%s: Error: 413 Video Requested Too Large", __func__);
                break;
            case DOWNLOAD_INTERNAL_ERR:
                fprintf(stdout, "Error: 500 Internal Server Error\n");
                syslog(LOG_ERR, "%s: Error: 500 Internal Server Error\n", __func__);
                break;
            default:
                fprintf(stdout, "Error: 404 Cannot Serve File\n");
                syslog(LOG_ERR, "%s: Default Case. Error: 404 Cannot Serve File.", __func__);                                                                
                break; 
        }
        
        return EXIT_FAILURE;
    }
    if (type == e_playback)
    {
        get_timestamp_str(range.start_time,start,sizeof(start));
        get_timestamp_str(range.end_time,end,sizeof(end));
    fprintf(stdout, "<?xml version=\"1.0\" encoding=\"utf-8\"?> \n \
    <video_stream> \n \
        <session_id>%s</session_id> \n \
        <start_time>%s</start_time> \n \
        <end_time>%s</end_time> \n \
        <stream_url>rtsp://localhost/playback/%s/%s.vra</stream_url> \n \
    </video_stream > \n", p_session_id,start,end,p_session_id,p_camera_name);    
    }

    return EXIT_SUCCESS;
}
