/**
 * @file    ra_data_search.c
 *
 * @author  rshafiq
 *
 * @section DESCRIPTION
 *
 * 
 * @section COPYRIGHT
 *
 * Copyright 2019 WABTEC Railway Electronics
 * This program is the property of WRE.
 */

#define _XOPEN_SOURCE 500
#include <ftw.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> 
#include <dirent.h>
#include <limits.h>

#ifdef VDOCK
#define DEBUG 0
    #define syslog(priority, ...)  \
        do { if (DEBUG) printf(__VA_ARGS__); } while (0)
    #define mkdir(a, ...)  \
        do {  mkdir(a); } while (0)
    #define symlink(...)  0
#else
#include <syslog.h>
#include <regex.h>
#include "reagent_config.h"
#endif


#include "video_api.h"


#define K_BYTES                             1024 // bytes
#define BUFF_SIZE                           512
#define BUFF_SIZE_SMALL                     16
#define BUFF_SIZE_BIG                       4096
#define EPOCH_START_YEAR                    1900
#define EPOCH_START_MONTH                   1
#define NFTW_DIRECTORY_DEPTH                20

static int rads_verbose;

// these globals were added to prevent a request from preventing further requests from breaking
// there is probably a better way, but due to time this is the best 
static time_t mtime_end = 0;
static time_t mtime_start = 0;

long time_epoch = 0;
char temp_buff[PATH_MAX];
static char     video_record_location[BUFF_SIZE] = {'\0'};

/**
 * Copy file from source to destination
 *
 * @param[in] source_file - source file
 * @param[in] dest_file   - destination file
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int copy_file(const char *source_file, const char *dest_file)
{   
    FILE *fptr_read, *fptr_write; 
    char buff[BUFF_SIZE_BIG] = {'\0'};
    int data_element_size = 1; //byte
    int rc = EXIT_SUCCESS;

    // Open one file for reading 
    fptr_read = fopen(source_file, "rb");

    if (fptr_read == NULL)
    { 
        syslog(LOG_NOTICE,"Cannot open source file %s \n", source_file); 
        return (EXIT_FAILURE); 
    } 

    // Open another file for writing 
    fptr_write = fopen(dest_file, "wb");

    if (fptr_write == NULL)
    { 
        syslog(LOG_NOTICE,"Cannot open file %s \n", dest_file);
        fclose(fptr_read); 
        return (EXIT_FAILURE);
    } 

    while (!feof(fptr_read))
    {
        size_t bytes = fread(buff, data_element_size, sizeof(buff), fptr_read);
        if (bytes)
        {
            if (fwrite(buff, data_element_size, bytes, fptr_write) != bytes)
            {
                syslog(LOG_NOTICE,"Copy failed! \n");
                rc = EXIT_FAILURE;
                break;            
            }
        }
    }

    fclose(fptr_read);
    fclose(fptr_write);
    return (rc); 
}

/**
 * Function to remove substring from main string 
 *
 * @param[in]   string - main string  
 * @param[in]   sub - sub string  
 *
 * @return    1 if found a substring and removed
 *          else 0;
 */
int remove_substr (char *string, char *sub) {
    char *match = string;
    int len = strlen(sub);
    int rc = 0;
    while ((match = strstr(match, sub))) {
        *match = '\0';
        strcat(string, match+len);
        match++;
        rc=1;
    }
    return(rc);
}

/**
 * Function to create directory recursively 
 *
 * @param[in]   dir - source  
 *
 * @return    None
 */
void _mkdir(const char *dir) {
    char tmp[256] = {'\0'};
    char *p = NULL;
    size_t len = 0;

    if (dir != NULL)
    {
        snprintf(tmp, sizeof(tmp), "%s", dir);
        len = strlen(tmp);
        if (tmp[len - 1] == '/')
        {
            tmp[len - 1] = 0;
        }
        for (p = (tmp + 1); *p; p++)
        {
            if (*p == '/')
            {
                *p = 0;
                if(mkdir(tmp, S_IRWXU) != EXIT_SUCCESS)//capturing return here, as it may fail
                {
                    syslog(LOG_ERR, "%s: mkdir failed (%s) (%d) at line (%d).\n", __func__, tmp, errno, __LINE__);
                }
                *p = '/';
            }
        }
        if(mkdir(tmp, S_IRWXU) != EXIT_SUCCESS)//capturing return here, as it may fail
        {
            syslog(LOG_ERR, "%s: mkdir failed (%s) (%d) at line (%d).\n", __func__, tmp, errno, __LINE__);
        }
    }
}

/**
 * Function make a symbolic link for source file to destination
 * directory (with source file name).
 *
 * @param[in]   file - source file path 
 * @param[out]  destination - destination directory path
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int mklink(const char file[PATH_MAX], char destination[DESTINATION_LENGTH])
{
    char file_link[PATH_MAX] = {'\0'};

    char *p_temp = strrchr(file, '/');
    if (p_temp == NULL)
    {
        return EXIT_FAILURE;
    }
    memcpy(file_link, destination, DESTINATION_LENGTH);

    if (strlen(p_temp) > ((PATH_MAX - 1) - strlen(file_link)))
    {
        return EXIT_FAILURE;
    }
    strncat(file_link, p_temp, strlen(p_temp));
    return symlink(file, file_link);
}

/**
 * Function to get file size in KB
 *
 * @param[in]   file_name - file name string to convert 
 * @param[out]  file_size - file size in KB
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int get_file_size(const char *file_name, uint64_t *file_size)
{
    struct stat st;
    int         ret_value = -1;

    //use stat function call to get the stat of file
    ret_value = stat(file_name, &st);

    if (ret_value != EXIT_SUCCESS)
    {
        return (EXIT_FAILURE);
    }

    *file_size = (st.st_size) / K_BYTES; // converting bytes into KB
    return (EXIT_SUCCESS);
}

#ifndef VDOCK
/**
 * Function to get current system time offset
 *
 * @return    offset
 */
long get_timeoffset(void)
{
    const char* s = getenv("TIME_INFO");
    char *time = NULL;
    long long int temp_offset = 0;
    if (s == NULL)
        return 0;
    time = strstr(s,"time_offset=");
    if (time == NULL)
        return 0;
    time += 12;
    
    return atol(time);
}
#endif

/**
 * Based on query provided this function will filter out and provide
 * the more close directory to find the video data faster. 
 *
 * @param[in]   va_download_t - download struct from download struct 
 * @param[out]  dir_path      - directory path to find video data
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int get_camera_dir_path(char *cam_name, char * root_dir, char * locoid,char (*dir_path)[PATH_MAX], uint8_t * dir_count)
{
    DIR *dfd;
    struct dirent *dp;
    int camera_count = 0;
    char temp_path[1024] ={'\0'};
    
    strncpy(temp_path, root_dir,1024);                        // /mnt/removableSSD/video/VideoFootages/
    strcat(temp_path,"/");

	// Loco ID is not added to the directory path
//    strncat(temp_path,locoid,10);                          // /mnt/removableSSD/video/VideoFootages/LOCOID_____/
    //strcat(temp_path,p_camdef->id);                          // /mnt/removableSSD/video/VideoFootages/LOCOID_____/ID.
    //strcat(temp_path,".");

    dfd = opendir(temp_path);

    if(dfd != NULL) 
    {
        
        while((dp = readdir(dfd)) != NULL)
        {
            if (strcmp(dp->d_name,"..") == 0 || strcmp(dp->d_name,".") == 0 || strcmp(dp->d_name,"METADATA") == 0)
                continue;
            if (strstr(dp->d_name,cam_name) != NULL)
                snprintf(dir_path[camera_count++], PATH_MAX-1,"%s/%s",temp_path,dp->d_name);
        }
        if (camera_count == 0)
        {
            closedir(dfd);
            return (EXIT_FAILURE);
        }
    }
    else
    {
        return (EXIT_FAILURE);
    }

    if (rads_verbose)
    {
        int i = 0;

        for (i = 0; i < camera_count; ++i)
            fprintf(stderr, "Camera Path %s\n", dir_path[i]);
    }
    *dir_count = (uint8_t)camera_count;
    closedir(dfd);
    return (EXIT_SUCCESS);
}


#ifndef VDOCK
/**
 * Function to match file naming 
 *
 * @param[in]   file - file name string to convert 
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int check_ra_file_regex(char const *file)
{
    regex_t regex;
    int reti,ret_val= EXIT_FAILURE;
    char msgbuf[100];

    if (file == NULL)
    {
        return -1;
    }

    /*Hard-cording RA file naming pattern. example: 20200131-040148.016013117.ra-*/
    reti = regcomp(&regex, "[0-9]{8}-[0-9]{6}.[0-9]{9}.[a-zA-Z]+", REG_EXTENDED);
    if (reti) {
        fprintf(stderr, "Could not compile regex\n");
    }
    else
    {
        /* Execute regular expression */
        reti = regexec(&regex, file, 0, NULL, 0);
        if (!reti) {
            ret_val = EXIT_SUCCESS;
        }
    }
    /* Free memory allocated to the pattern buffer by regcomp() */
    regfree(&regex);
    return ret_val;

}
#endif


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
int file_name_to_epoch(const char *file_name, long int *epoch)
{
    //TODO is there better way to do it?

    struct tm t;
    time_t    t_of_day;
    int       v_date      = 0;
    const int const_10000 = 10000;
    const int const_100   = 100;
    const int index_time  = 9;
#ifndef VDOCK
    if (0 != check_ra_file_regex(file_name))
    {
        return(EXIT_FAILURE);
    }
#endif
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
    //printf("\n%s[%d]year = %d\n Month = %d\n Day   = %d\n Hour  = %d\n Min   = %d\n Sec   = %d", __func__,__LINE__,(int)t.tm_year, (int)t.tm_mon, (int)t.tm_mday, (int)t.tm_hour,(int)t.tm_min,(int)t.tm_sec);
    *epoch = (long int)t_of_day;

    return (EXIT_SUCCESS);
}

/**
 * epoch_to_icd_format.
 *
 * @param[in] rawtime 
 * @param[in] size 
 * @param[out] formated_time
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int epoch_to_icd_format(time_t rawtime, char *formated_time, size_t size)
{
    struct tm  ts;
    char       buff[80]={'\0'};

    // Format time, "ddd yyyy-mm-dd hh:mm:ss zzz"
    ts = *localtime(&rawtime);
    strftime(buff, 80, "%Y%m%dT%H%M%S", &ts);
    strncpy(formated_time,buff,size);
    return 0;
}

/**
 * Function call back for ntwf and apply filter to find the files
 * this function go through the directories recursively and find the 
 * desired video 
 *
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
static int get_oldest_timestamp (const char        *fpath,
                                const struct stat *sb,
                                int                tflag,
                                struct FTW        *ftwbuf)
{
    char     *file_name;
    long int  file_time_stamp      =    0;
    if (tflag == FTW_F && (mtime_start == 0 || sb->st_mtime < mtime_start) )
    { 
        file_name = strrchr(fpath, '/') + 1;
        if (file_name_to_epoch(file_name,&file_time_stamp) == 0)
        {
            mtime_start = sb->st_mtime;
            memcpy(temp_buff, fpath, PATH_MAX);
            time_epoch = file_time_stamp;
        }
    }
    return 0;  
}

/**
 * Wrapper function for nftw
 *
 * @param[in] path to parent directory
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int get_data_start_time(const char *path, char * return_string, long int *e_time)
{
	//reset mtime variable
	mtime_start = 0;
	char *t;
    if (nftw(path, get_oldest_timestamp, NFTW_DIRECTORY_DEPTH , 0) == EXIT_FAILURE)
    {
        perror("nftw");
        return (EXIT_FAILURE);
    }
    strncpy(return_string, temp_buff, PATH_MAX);
	t = strrchr(temp_buff, '/') + 1;
	file_name_to_epoch((const char*)t, e_time);
    return (EXIT_SUCCESS);
}




/**
 * Function call back for ntwf and apply filter to find the files
 * this function go through the directories recursively and find the 
 * desired video 
 *
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
static int get_newest_timestamp (const char        *fpath,
                                const struct stat *sb,
                                int                tflag,
                                struct FTW        *ftwbuf)
{
    char     *file_name;
    long int  file_time_stamp      =    0;


    if (tflag == FTW_F && (mtime_end == 0 || sb->st_mtime > mtime_end) )
    { 
        file_name = strrchr(fpath, '/') + 1;
        if (file_name_to_epoch(file_name,&file_time_stamp) == 0)
        {
            mtime_end = sb->st_mtime;
            memcpy(temp_buff, fpath, PATH_MAX);
            time_epoch = file_time_stamp;
        }
    }
    return 0;  
}

/**
 * Wrapper function for nftw
 *
 * @param[in] path - path to parent directory 
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int get_data_end_time(const char *path, char * return_string, long int *e_time)
{
	//reset the mtime variable
	mtime_end = 0;
	char *t;
    if (nftw(path, get_newest_timestamp, NFTW_DIRECTORY_DEPTH , 0) == EXIT_FAILURE)
    {
        perror("nftw");
        return (EXIT_FAILURE);
    }
	
    strncpy(return_string, temp_buff, PATH_MAX);
	t = strrchr(temp_buff, '/') + 1;
	file_name_to_epoch((const char*)t, e_time);
    return (EXIT_SUCCESS);

    
}

/**
 * string the extension of file from file name
 *
 * @param[in/out] fname - epoch time
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
void strip_ext(char *fname)
{
    char *end = fname + strlen(fname);

    while (end > fname && *end != '.' && *end != '\\' && *end != '/') {
        --end;
    }
    if ((end > fname && *end == '.') &&
        (*(end - 1) != '\\' && *(end - 1) != '/')) {
        *end = '\0';
    }  
}


/**
 * trim_trailing_undersore
 *
 * @param[in] v_string 
 * @param[in] len 
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int trim_trailing_undersore(char * v_string, int len)
{
    int i ;

    for (i = 0; i < len; ++i)
    {
        if (v_string[i] == '_')
        {
            if (v_string[i+1] == '\0' || v_string[i + 1] == '_' || i+1 >= len)
                v_string[i] = '\0';
        }
    }
    return 0;
}

/**
 * add_trailing_underscore
 *
 * @param[in] v_string 
 * @param[in] len 
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int add_trailing_underscore(char * v_string, int len)
{
    int i ;

    for (i = strlen(v_string); i < len; ++i)
    {
        v_string[i] = '_';
    }
    return 0;
}



/*
int main()
{
    char return_data[PATH_MAX];
    /////////////// TEST ////////////////////////
    
    ////////     Get newest file //////
    get_data_start_time("../BNSF_1234567/FCAB_FORWARD.BADBADBADBAD",return_data); 
    fprintf(stderr,"\n%s\n%ld\n", return_data,time_epoch);

    //////////// Get oldest file  ////////////
    get_data_end_time("../BNSF_1234567/FCAB_FORWARD.BADBADBADBAD",return_data); 
    fprintf(stderr,"\n%s\n%ld\n", return_data,time_epoch);

    return 0;

}
*/
