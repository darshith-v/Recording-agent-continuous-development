#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <libgen.h>

#include "common_defs.h"

#define MAX_TIME_T  2147483647
#define MAX_TRACKS  16
#define MAX_DRIVES  2
#define MAX_CHUNKS  65536
#define MAX_GAPS    MAX_CHUNKS / 2
#define micro_secs_per_second  1000000
#define CAMERA_NAME_LENGTH 12


char* DRIVE_PATH_ARRAY[MAX_DRIVES] =  {
                                        CHM_PATH,
                                        RSSD_PATH
                                        };

char* DRIVE_NAME_ARRAY[MAX_DRIVES] =  {
                                        "CHM",
                                        "SSD"
                                        };


typedef struct 
{
	time_t          start_time;
	uint64_t        duration;		// seconds
} single_chunk_t;	

typedef struct
{
    single_chunk_t  recording[MAX_CHUNKS];
    uint16_t        gaps[MAX_GAPS];      //keeps track of the continuous recording index
    uint16_t        num_chunks;
    char            camera_name[CAMERA_NAME_LENGTH + 1]; // add one for null
} cam_rec_t;

cam_rec_t TIME_ARRAY[MAX_TRACKS];

/**
 * Checks to see if the VideoFootages directory exists as a test of drive mount
 * 
 * @param drive 0 - CHM 1 - RSSD
 * @return int 
 */
int get_drive_availability(uint8_t drive)
{
    DIR *d = NULL;
    d = opendir(DRIVE_PATH_ARRAY[drive]);
    if (d)
    {
        closedir(d);
        return (1);
    }
    
    return (0);
}

/**
 * Compares two chunks' start times to determine age
 * Used for the qsort function
 * 
 * @param chunk1 
 * @param chunk2 
 * @return int 
 */
int compare_chunks(const void* chunk1, const void* chunk2)
{
    single_chunk_t* first_compare = (single_chunk_t*)chunk1;
    single_chunk_t* second_compare = (single_chunk_t*)chunk2;
    return (first_compare->start_time - second_compare->start_time);
}

/**
 * Gets the file duration from the file name
 * 
 * @param file_path 
 * @return long     file duration
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
 * Converts a file name into the epoch time
 * 
 * @param file_path 
 * @return time_t epoch timestamp
 */
time_t file_name_to_epoch(const char *file_path)
{
    if (!file_path)
    {
        return MAX_TIME_T;
    }
    char *file_name = strrchr(file_path, '/');
    if (!file_name)
    {
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

    // parse the file name and put the values in time struct
    // then covert it to epoch time
    v_date     = atoi(file_name);
    t.tm_year  = v_date / const_10000 - 1900;
    t.tm_mon   = (v_date % const_10000) / const_100 - 1;
    t.tm_mday  = v_date % const_100;
    v_date     = atoi(file_name + index_time);
    t.tm_hour  = v_date/const_10000;
    t.tm_min   = (v_date % const_10000) / const_100;
    t.tm_sec   = v_date % const_100;
    t.tm_isdst = -1;        // Is DST on? 1 = yes, 0 = no, -1 = unknown
    t_of_day   = mktime(&t);
    
    return t_of_day;
}

/**
 * @brief Converts an epoch time to the standard timestamp
 * 
 * @param epoch_seconds epoch timestamp
 * @return const char*  YYYYMMDDTHHMMSS
 */
static const char* epoch_to_timestamp(const time_t epoch_seconds)
{
    static char time_buffer[32] = {0};
    memset(&time_buffer,0,32);

    time_t rawtime = epoch_seconds;
    struct tm timestamp;
    setenv("TZ","GMT+0",1);

    localtime_r(&rawtime, &timestamp);
    strftime(time_buffer, sizeof(time_buffer)-1, "%Y%m%dT%H%M%S", &timestamp);

    return time_buffer;
}

/**
 * Recursively searches the given path for chunk data and populates the time array
 * 
 * @param dir_path          
 * @param camera_increment 
 */
void search_directory (char* dir_path, int* camera_increment)
{
    DIR* d = NULL;
    struct stat sb_current;
	single_chunk_t current_file_info = {.start_time = 0, .duration = 0};
    static int current_chunk_index = 0;
    time_t end_time = MAX_TIME_T, difference_time = MAX_TIME_T;
    int i=0;

    d = opendir(dir_path);
    if (!d)
    {
        return;
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
        char path[PATH_MAX] = "\0";
        path_length = snprintf(path,PATH_MAX,"%s/%s",dir_path,d_name);
		
        if (path_length <= 0 || path_length > PATH_MAX) // should skip this dir b/c all files have same string length
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
				search_directory(path, camera_increment);
				if (strcmp(dir_path, DRIVE_PATH_ARRAY[0]) == 0 || strcmp(dir_path, DRIVE_PATH_ARRAY[1]) == 0)
				{
                    strncpy(TIME_ARRAY[*camera_increment].camera_name, d_name, CAMERA_NAME_LENGTH);

		    i=CAMERA_NAME_LENGTH ;
                    while(TIME_ARRAY[*camera_increment].camera_name[--i]== '_')
                    	TIME_ARRAY[*camera_increment].camera_name[i]='\0';
                    *camera_increment += 1;
                    current_chunk_index = 0;        //reset the chunk array for new camera
				}
			}
			else if (S_ISREG(sb_current.st_mode))
			{
                TIME_ARRAY[*camera_increment].recording[current_chunk_index].start_time = file_name_to_epoch(&path[0]);
                TIME_ARRAY[*camera_increment].recording[current_chunk_index].duration = get_file_duration(&path[0]);
                TIME_ARRAY[*camera_increment].num_chunks++;
                current_chunk_index++;
			}
		}
    }
	
	closedir(d);
}

/**
 * Prints out the information for a single drive
 * 
 * @param drive 0 - CHM, 1 - RSSD
 */
void print_ssd(int drive)
{
    if (drive > MAX_DRIVES)
    {
        return;
    }

    //clean the time array and set up
    memset(&TIME_ARRAY[0],0,sizeof(TIME_ARRAY));
    int camera_increment = 0;
    int i = 0, j = 0;

    // traverse through the filesystem recursively and build the time array
    search_directory(DRIVE_PATH_ARRAY[drive], &camera_increment);
    for (i = 0; i < camera_increment; i++)
    {
        // use the standard sorting mechanism to put the array into order of start time
        qsort(TIME_ARRAY[i].recording, TIME_ARRAY[i].num_chunks,sizeof(single_chunk_t),compare_chunks);
		if (TIME_ARRAY[i].num_chunks == 0)
            continue;
        printf("\t<segments>\n");
        printf("\t\t<name>%s</name>\n",&TIME_ARRAY[i].camera_name[0]);
        printf("\t\t<location_name>%s</location_name>\n",DRIVE_NAME_ARRAY[drive]);
        printf("\t\t<video_segments>\n");
        printf("\t\t\t<start_time>%s</start_time>\n",epoch_to_timestamp(TIME_ARRAY[i].recording[j].start_time));

        for (j = 1; j < TIME_ARRAY[i].num_chunks ; j++)
        {
            if (abs(TIME_ARRAY[i].recording[j].start_time - (TIME_ARRAY[i].recording[j-1].start_time + (TIME_ARRAY[i].recording[j-1].duration/micro_secs_per_second))) > 1)
            {  // if the current chunk start time is > 1 second in either direction of the end time of the previous segment   
                printf("\t\t\t<end_time>%s</end_time>\n", epoch_to_timestamp((TIME_ARRAY[i].recording[j-1].start_time + ((time_t)(TIME_ARRAY[i].recording[j-1].duration / micro_secs_per_second)))));
                printf("\t\t</video_segments>\n");
                printf("\t\t<video_segments>\n");
                printf("\t\t\t<start_time>%s</start_time>\n",epoch_to_timestamp(TIME_ARRAY[i].recording[j].start_time));
            }
	   
	 }
        
        printf("\t\t\t<end_time>%s</end_time>\n", epoch_to_timestamp((TIME_ARRAY[i].recording[j-1].start_time + ((time_t)( TIME_ARRAY[i].recording[j-1].duration) / micro_secs_per_second))));

        printf("\t\t</video_segments>\n");
        printf("\t</segments>\n");
        j = 0;
        
    }
    
    
}

void print_recordings()
{
    print_ssd(eCHM);

    if (get_drive_availability(eRSSD))
    {
        print_ssd(eRSSD);
    }

    return;
}

int main(int argc, char* argv[])
{
    printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    printf("<recordings_list>\n");
    print_recordings();  
    printf("</recordings_list>\n");
	
	return 0;

}