#define _XOPEN_SOURCE 500
#include <ftw.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifndef QNX_BUILD
#include <poll.h>
#include <time.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <unistd.h> 
#include <linux/limits.h>
#include <inttypes.h>
#include <syslog.h>
#include <libgen.h>
#include <fcntl.h>
#include <time.h>

#include "reagent_config.h"
#include "common_defs.h"
#include "video_api.h"

#define MID_BUFF                    256
#define FREE_SPACE_LIMIT            20
#define MAX_TIME_T                  2147483647
#define MAX_DRIVES                  2
#define PATH_MAX_RA                 1024
#define MON_SLEEP            60       // 60 sec
#define GROOMING_CTRL_FILE "/tmp/grooming_not_allowed"
#define MAX_CAMERA_COUNT            16

int verbose = 0;
char *dir_list[MAX_CAMERA_COUNT];
static int space_threshold = FREE_SPACE_LIMIT; // 20% is default value
static int dir_count = 0;

typedef struct
{
	int dirc;
	char **dir_path;
} monitor_t;

monitor_t space_mon;

typedef struct
{
    char            full_path[CAMERA_NAME_LENGTH + MAC_LEN + 2]; // add one for null and one for the "." (CAM_________.MAC)
    uint64_t        max_usec;
} time_based_camera_t;

const char* DRIVE_ARRAY[MAX_DRIVES] = {CHM_PATH, RSSD_PATH};

/**
 * @brief Retrieves available space for a filesystem based on a path
 * 
 * @param path - Path to filesystem
 * @param stat - pointer to FS stat structure
 * @return float - available space as percentage
 */
float process_available_space(const char* path, struct statvfs* stat)
{
    if (statvfs(path,stat) != 0)
    {
        return EXIT_FAILURE;
    }

    return ((float)stat->f_bavail/(float)stat->f_blocks)*100;
}

/**
 * @brief Retrieves the duration of a chunk based on filename
 * 
 * @param file_path -- path to the file
 * @return long     -- duration in microseconds
 */
long get_file_duration(const char* file_path)
{
	if (!file_path)
    {
        return -1;
    }
    char *file_name = strrchr(file_path, '/');
    if (!file_name)
    {	        
        return -1;
    }
    file_name++;

    const int index_duration = 16;
    long v_duration = 0;

    v_duration = atol(file_name + index_duration);
    return v_duration;
}

/**
 * @brief Checks if a directory is empty
 * 
 * @param dirname - path to directory
 * @return int    - 1 if empty, 0 otherwise
 */
int is_dir_empty(char *dirname) {
  int n = 0;
  struct dirent *d;
  DIR *dir = opendir(dirname);
  if (dir == NULL) //Not a directory or doesn't exist
    return 0;
  while ((d = readdir(dir)) != NULL) {
    if(++n > 2)
      break;
  }
  closedir(dir);
  if (n <= 2) //Directory Empty
    return 1;
  else
    return 0;
}

/**
 * @brief Converts a file name to an epoch timestamp
 * 
 * @param file_path -- path to a file
 * @return time_t   -- start time of file as epoch timestamp
 */
time_t file_name_to_epoch(const char *file_path)
{
    if (!file_path)
    {
        if(verbose)
            fprintf(stderr,"%s: File path invalid\n", __func__);
        syslog(LOG_ERR,"%s: File path invalid\n", __func__);
        return MAX_TIME_T;
    }
    char *file_name = strrchr(file_path,'/');
    if (!file_name)
    {
        if(verbose)
            fprintf(stderr,"%s: Unable to retreive filename\n", __func__);
        syslog(LOG_ERR,"%s: Unable to retreive filename\n", __func__);
        return MAX_TIME_T;
    }
    file_name++;
    struct tm t;
    time_t    t_of_day;
    int       v_date      = 0;
    
    //constants needed for conversion
    const int EPOCH_YEAR = 1900;
    const int EPOCH_MONTH = 1;
    const int const_10000 = 10000;
    const int const_100   = 100;
    const int index_time  = 9;

    //convert each section into appropriate tm struct variables
    v_date     = atoi(file_name);
    t.tm_year  = v_date / const_10000 - 1900;
    t.tm_mon   = (v_date % const_10000) / const_100 - 1;
    t.tm_mday  = v_date % const_100;
    v_date     = atoi(file_name + index_time);
    t.tm_hour  = v_date/const_10000;
    t.tm_min   = (v_date % const_10000) / const_100;
    t.tm_sec   = v_date % const_100;
    t.tm_isdst = -1;        // Is DST on? 1 = yes, 0 = no, -1 = unknown

    //convert tm struct into time_t value
    t_of_day   = mktime(&t);
    return t_of_day;
}

/**
 * @brief Recursively searches for the oldest file in a given directory
 * 
 * @param dir_path          -- path to search
 * @param oldest_file       -- pointer to oldest file path
 * @param oldest_file_time  -- pointer to oldest file time
 * @return void* 
 */
void* find_oldest_file (char* dir_path, char* oldest_file, time_t* oldest_file_time)
{
    DIR* d = NULL;
    struct stat sb_current;
    time_t current_file_time = MAX_TIME_T;

    d = opendir(dir_path);
    if (!d)
    {
        if (verbose)
		    fprintf(stderr, "%s: unable to open '%s'\n", __func__, dir_path);
        syslog(LOG_ERR, "%s: unable to open '%s'\n", __func__, dir_path);
        return NULL;
    }

    while(1)
    {
        struct dirent* entry = NULL;
        char* d_name = NULL;
        entry = readdir(d);
        if (!entry)
            break;

        d_name = entry->d_name;
		
        int path_length = -1;
        char path[PATH_MAX_RA] = "\0";
        path_length = snprintf(path,PATH_MAX_RA,"%s/%s",dir_path,d_name);
		
        if (path_length <= 0 || path_length > PATH_MAX_RA) // should skip this dir b/c all files have same string length
        {
            if (verbose)
                fprintf(stderr,"%s: Invalid path length in dir '%s'\n",__func__,dir_path);
            syslog(LOG_ERR,"%s: Invalid path length in dir '%s'\n",__func__,dir_path);
            break;
        }
        
		if (stat(path,&sb_current) != 0)
		{
            if (verbose)
                fprintf(stderr,"%s: Unable to stat path in dir '%s'\n",__func__,dir_path);
            syslog(LOG_ERR,"%s: Unable to stat path in dir '%s'\n",__func__,dir_path);
			break;
		}
		
		
		if ((strcmp(d_name,"..") == 0) || (strcmp(d_name, ".") == 0) || (strcmp(d_name,"lost+found") == 0) || (strcmp(d_name,"staging") == 0))
		{
			continue;
		}
		else
		{
			if (S_ISDIR(sb_current.st_mode))
			{
				
				find_oldest_file(path, oldest_file,oldest_file_time);
			}
			else if (S_ISREG(sb_current.st_mode))
			{
                current_file_time = file_name_to_epoch(&path[0]);
                if ((current_file_time != MAX_TIME_T) && (current_file_time < *oldest_file_time))
				{
					*oldest_file_time = current_file_time;
					strncpy(oldest_file,path,PATH_MAX_RA);
				}
			}
		}
    }

    if (closedir(d) != 0)
    {
        if (verbose)
            fprintf(stderr,"UNABLE TO CLOSE %s (%s)", dir_path,strerror(errno));
        syslog(LOG_ERR,"UNABLE TO CLOSE %s (%s)", dir_path,strerror(errno));
    }
    return NULL;
}

/**
 * @brief Searches for and deletes the oldest file on a drive and camera (if specified)
 * 
 * @param drive     -- 0 for CHM, 1 for rSSD
 * @param camera    -- NULL for entire drive, camera.mac name otherwise
 * @return int      -- EXIT_SUCCESS on success, EXIT_FAILURE otherwise
 */
int delete_oldest_file (uint8_t drive, const char* camera)
{
    int rc = EXIT_FAILURE;
	if (drive > 1)
	{
		return rc;
	}
	
	
    int dir_is_empty = -1;
    int dir_level = 0;
    char oldest_path[PATH_MAX_RA] = {0}, path_to_search[PATH_MAX_RA] = {0};
    char* oldest_file_dir = NULL;
    time_t oldest_file_time = MAX_TIME_T;
    time_t *oldest_file_time_ptr = &oldest_file_time;
    char* oldest_file = &oldest_path[0];
    //build the path 
    if (camera != NULL)
        snprintf(path_to_search, PATH_MAX_RA, "%s/%s/", DRIVE_ARRAY[drive], camera);
    else
        snprintf(path_to_search, PATH_MAX_RA, "%s/", DRIVE_ARRAY[drive]);

    // find the oldest file
    find_oldest_file(path_to_search,oldest_file,oldest_file_time_ptr);

    // delete the oldest file
    if (remove(oldest_file) != 0)
        {
            rc = EXIT_FAILURE;
            if(verbose)
                fprintf(stderr,"%s: Unable to remove '%s' (%s)\n",__func__,oldest_file,strerror(errno));
            syslog(LOG_ERR,"%s: Unable to remove '%s' (%s)\n",__func__,oldest_file,strerror(errno));
        }
        else // time to delete the directories if they can be
        {
            rc = EXIT_SUCCESS;
            if (verbose)
                fprintf(stderr, "%s: Successfully removed '%s'\n", __func__, oldest_file);
            syslog(LOG_ERR, "%s: Successfully removed '%s'\n", __func__, oldest_file);

            // attempt to delete up to and including camera name
            oldest_file_dir = dirname(oldest_file);
            for (dir_level = 0; dir_level < 5; dir_level++)
            {
                if ((oldest_file_dir != NULL) && (strcmp(basename(oldest_file_dir), (const char*)"VideoFootages") != NULL))
                {
                    dir_is_empty = is_dir_empty(oldest_file_dir);
                    if (dir_is_empty == 0)
                    {
                        if(verbose)
                            fprintf(stderr,"%s: '%s' is not empty or does not exist.\n",__func__,oldest_file_dir);
                        syslog(LOG_ERR,"%s: '%s' is not empty or does not exist.\n",__func__,oldest_file_dir);
                        break;
                    }
                    else
                    {
                        if (rmdir(oldest_file_dir) != 0)
                        {
                            if (verbose)
                                fprintf(stderr, "%s: Unable to remove '%s' (%s)\n", __func__,oldest_file_dir,strerror(errno));
                            syslog(LOG_ERR, "%s: Unable to remove '%s' (%s)\n", __func__,oldest_file_dir,strerror(errno));
                        }
                        else 
                        {
                            if (verbose)
                                fprintf(stderr, "%s: (%s) removed successfully\n",__func__, oldest_file_dir);
                            syslog(LOG_ERR, "%s: (%s) removed successfully\n",__func__, oldest_file_dir);
                        }
                    }
                }
                else
                {
                    if (verbose)
                        fprintf(stderr,"%s: Unable to retrieve dirname for '%s'\n",__func__, oldest_file);
                    syslog(LOG_ERR,"%s: Unable to retrieve dirname for '%s'\n",__func__, oldest_file);
                }
                oldest_file_dir = dirname(oldest_file_dir);
            }
        }


    return rc;
}

/**
 * @brief handles processing of available space to determine if files should be deleted from a drive
 * 
 * @param base  -- top level directory of drive filesystem
 */
void space_handler(const char* base)
{
	float	space_available = 0;
	struct 	statvfs stat;
	
	uint8_t drive = 0;
	
	drive = (strstr(base,"chm") == NULL) ? eRSSD: eCHM;
	
	space_available = process_available_space(base, &stat);
	
	if (space_available != EXIT_FAILURE)
	{
		
		while (space_available < space_threshold)
		{
            if (verbose)
                fprintf(stderr, "%s: At Path [%s]: Available space percentage %f/%u percent\n", __func__, base, space_available, space_threshold);
            syslog(LOG_NOTICE, "%s: At Path [%s]: Available space percentage %f/%u percent\n", __func__, base, space_available, space_threshold);
			if (delete_oldest_file(drive, NULL) != EXIT_SUCCESS)
			{
				break;
			}
			else
			{
				space_available = process_available_space(base, &stat);
			}
		}
        
	}
	else
	{
        if (verbose)
		    fprintf(stderr, "%s: Error: Unable to retrieve free space\n", __func__);
        syslog(LOG_ERR, "%s: Error: Unable to retrieve free space\n", __func__);
	}	
}

/**
 * @brief Calculates total duration of a camera
 * 
 * @param dir_path  -- full path to a camera directory
 * @return uint64_t -- total duration in microseconds
 */
uint64_t process_duration (char* dir_path)
{
    DIR* d = NULL;
    struct stat sb_current;
    uint64_t duration = 0;

    d = opendir(dir_path);
    if (!d)
    {
        return 0;
    }

    while(1)
    {
        struct dirent* entry = NULL;
        char* d_name = NULL, *prev_d_name = NULL;
        entry = readdir(d);
        if (!entry)
            break;
        prev_d_name = d_name;
        d_name = entry->d_name;
		
        int path_length = -1;
        char path[PATH_MAX_RA] = "\0";
        path_length = snprintf(path,PATH_MAX_RA,"%s/%s",dir_path,d_name);
		
        if (path_length <= 0 || path_length > PATH_MAX_RA) // should skip this dir b/c all files have same string length
        {
            break;
        }
        
		if (stat(path,&sb_current) != 0)
		{
			break;
		}
		
		
		if ((strcmp(d_name,"..") == 0) || (strcmp(d_name, ".") == 0) || (strcmp(d_name,"lost+found") == 0) || (strcmp(d_name,"staging") == 0) || (strcmp(d_name,"METADATA") == 0))
		{
			continue;
		}
		else
		{
			
			if (S_ISDIR(sb_current.st_mode))
			{
				duration += process_duration(path);
			}
			else if (S_ISREG(sb_current.st_mode))
			{
                duration += get_file_duration(path);
			}
		}
    }

    closedir(d);
    return duration;
}

/**
 * @brief Handles processing of total duration and deciding if time based grooming needs to occur for a given camera with time requirements
 * 
 * @param base  -- top level directory of filesystem
 */
void groom_handler(const char* base, time_based_camera_t camera_info)
{
    int i;  // timebasedarray index
    uint64_t current_duration = 0;
    char full_path[PATH_MAX_RA];
    uint8_t drive = 0;
	
	drive = (strstr(base,"chm") == NULL) ? eRSSD: eCHM;
    memset(&full_path[0], 0, PATH_MAX_RA);

    // build track path
    snprintf(full_path, PATH_MAX_RA, "%s/%s", base, camera_info.full_path);
    // process total stored duration
    current_duration = process_duration(full_path);
    
    while (current_duration > camera_info.max_usec)
    {
        if (verbose)
            fprintf(stderr, "%s: At Path [%s]: Current Duration %ld/%ld microseconds\n", __func__, base, current_duration, camera_info.max_usec);
        syslog(LOG_NOTICE, "%s: At Path [%s]: Current Duration %ld/%ld microseconds\n", __func__, base, current_duration, camera_info.max_usec);
        delete_oldest_file(drive, camera_info.full_path);
        current_duration = process_duration(full_path);
    }
}

void usage( char *in )
{
    fprintf(stderr, "usage: %s [h] [v] [-l <x%%>]  <dir1> <dir2>...\n "
        "\twhere:\n"
        "\t\t-h -- print this message\n"
        "\t\t-v -- verbose on\n"
        "\t\t-l -- limit -- free space limit in percentage. Default value is 20%%. If value provided less than 20\n"
        "\t\t               it will remain 20%%\n",in );
}

/**
 * parse program input parameters 
 *
 * @param[in] argc
 * @param[in] argv
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int parse_arg(int argc, char* const *argv)
{
    int opt;
    while ((opt = getopt (argc, argv, "h?vl:")) != -1)
    {   
        switch (opt)
        {
            case 'v':
                verbose = 1;
                break;
            /* free space limit in percentage */
            case 'l':
                space_threshold = atoi(optarg);
                if (space_threshold < 20 || space_threshold == 999999) {
                    space_threshold = 20;
                }               
                break;
            case 'h':
            case '?':
            default:
                usage(argv[0]);
                return EXIT_SUCCESS;
                break;
        }
        
    }

    if (optind < argc) {
        int i = 0;
        while (optind <= argc && argv[optind] != NULL && i< 16)
        {
            dir_list [i] = argv[optind];
            if(verbose)
                fprintf(stderr, "dir[%d]: %s\n",i,dir_list[i] );
            syslog(LOG_ERR, "dir[%d]: %s\n",i,dir_list[i] );
            i += 1;
            optind += 1;
        }
        dir_count = i;    
    }
    else
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int main(int argc, char* const* argv)
{
	media_info_t* p_minfo = NULL;
	camdef_t* p_camdef = NULL;
	int i = 0, rc = 0, j = 0;
	uint64_t crt = 0;		// time-based grooming
    char cname[13] = {0};
	int time_based_idx = 0;
    time_based_camera_t	TIME_BASED_ARRAY[MAX_CAMERA_COUNT];			// the cameras that need special time based grooming
    int num_time_based_cams = 0;
	
	char *targv[MID_BUFF] = {argv[0],"/opt/reagent/config/current_config.xml"};
	memset(TIME_BASED_ARRAY,0,sizeof(TIME_BASED_ARRAY));
    memcpy(cname, "____________", sizeof(cname) - 1);
		
	if (parse_arg(argc,argv) == EXIT_FAILURE)
	{
		return EXIT_FAILURE;
	}
	
	space_mon.dirc = dir_count;
	space_mon.dir_path = (char**)dir_list;
	
	raconf_parse(2,(char **)targv);
	p_minfo = raconf_get_media_info(0);
	
	if (!p_minfo)
	{
		return EXIT_FAILURE;
	}

	if (verbose)
        raconf_show();

	for (i = 0, p_camdef=&(p_minfo->cameras[0]); i < p_minfo->n_cameras; ++p_camdef, ++i)
	{
		if (strlen(p_camdef->disp_mac) > 0)
		{
			memcpy(cname, p_camdef->name, sizeof(cname));
            if (p_camdef->max_age[0])
            {
                if (sscanf(p_camdef->max_age, "%" SCNu64, &crt) != 1)
                {
                    syslog(LOG_WARNING, "%s: sscanf error (%d).\n", __func__, errno);
                }
                crt = crt * 60 * 60 * 1000000;
                if (crt > 0)
                {
                    TIME_BASED_ARRAY[time_based_idx].max_usec = crt;
                    snprintf(TIME_BASED_ARRAY[time_based_idx].full_path, CAMERA_NAME_LENGTH + MAC_LEN + 1, "%s.%s", cname,p_camdef->disp_mac);
                    if (verbose)
                        fprintf(stderr,"Space Monitor: Camera Name (%s) has max age of (%ld)\n", TIME_BASED_ARRAY[time_based_idx].full_path, TIME_BASED_ARRAY[time_based_idx].max_usec);
                    syslog(LOG_NOTICE,"Space Monitor: Camera Name (%s) has max age of (%ld)\n", TIME_BASED_ARRAY[time_based_idx].full_path, TIME_BASED_ARRAY[time_based_idx].max_usec);
                    time_based_idx++;
                }
			}
		}		
	}
	
    num_time_based_cams = time_based_idx;
    for (;;)
    {
        // this will cycle its loop once per minute, alternating which drive is done
        for (i = 0; i < space_mon.dirc; i++)
        {
            if( access(GROOMING_CTRL_FILE, F_OK ) == -1)
            {    
                    space_handler(space_mon.dir_path[i]);
                    for (j = 0; j < num_time_based_cams; j++)
                    {
                        groom_handler(space_mon.dir_path[i], TIME_BASED_ARRAY[j]);
                    }
            }
            else
            {
                    syslog(LOG_NOTICE, "%s: Drive Validation is in progress ...", __func__);
            }
            sleep(MON_SLEEP);		// sleep for 60 seconds
        }
    }
	return 0;
}

#else
// do nothing for now if built for QNX
int main(int argc, char* const *argv)
{
	for (;;) {
		sleep(5) ;
	}
	return 0 ;
}

#endif // QNX_BUILD
