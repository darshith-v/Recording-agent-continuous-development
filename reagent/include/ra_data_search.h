/**
 * @file    ra_data_search.h
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

#ifndef RA_DATASEARCH_H
#define RA_DATASEARCH_H

#include <limits.h>

#include "ptc_intf.h"
#include "video_api.h"

#define K_BYTES                             1024 // bytes
#define BUFF_SIZE                           512
#define BUFF_SIZE_SMALL                     16
#define BUFF_SIZE_BIG                       4096
#define EPOCH_START_YEAR                    1900
#define EPOCH_START_MONTH                   1
#define NFTW_DIRECTORY_DEPTH                20

extern int ra_verbose;
/**
 * Copy file from source to destination
 *
 * @param[in] source_file - source file
 * @param[in] dest_file   - destination file
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int copy_file(const char *source_file, const char *dest_file);
/**
 * Function to remove substring from main string 
 *
 * @param[in]   string - main string  
 * @param[in]   sub - sub string  
 *
 * @return    1 if found a substring and removed
 *          else 0;
 */
int remove_substr (char *string, char *sub);
/**
 * Function to get file size in KB
 *
 * @param[in]   file_name - file name string to convert 
 * @param[out]  file_size - file size in KB
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int get_file_size(const char *file_name, uint64_t *file_size);
#ifndef VDOCK
/**
 * Function to get current system time offset
 *
 * @return    offset
 */
long get_timeoffset(void);
#endif
/**
 * This function will looking into parent directory(CHM or RSSD) and current configuration 
 * to findout the camera name. if found it will provide the directory path 
 *
 * @param[in]   va_download_t - download struct from download struct 
 * @param[out]  dir_path      - directory path to find video data
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int get_camera_dir_path(char *cam_name, char * root_dir, char * locoid,char (*dir_path)[PATH_MAX], uint8_t * dir_count);

/**
 * Function to match file naming to ReAgent specific file naming scheme. 
 * example: 20200131-040148.016013117.ra-
 *
 * @param[in]   file - file name string to convert 
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int check_ra_file_regex(char const *file);

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
int file_name_to_epoch(const char *file_name, long int *epoch);
/**
 * Wrapper function for nftw. it will return latest file in provided directory
 * this function will also convert file name to epoch timestamp and return.
 *
 * @param[in] path to parent directory
 * @param[out] return_string -- path of newest file in parent dir
 * @param[out] e_time -- epoch time converted from file name
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int get_data_start_time(const char *path, char * return_string, long *e_time);

/**
 * Wrapper function for nftw. it will return oldest file in provided directory.
 * this function will also convert file name to epoch timestamp and return.
 *
 * @param[in] path - path to parent directory 
 * @param[out] return_string -- path of newest file in parent dir
 * @param[out] e_time -- epoch time converted from file name
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int get_data_end_time(const char *path, char * return_string, long * e_time);

/**
 * converts the epoch time into YYYYmmDDTHHMMSS format
 *
 * @param[in] rawtime - epoch time
 * @param[out] formated_time -- formated time string
 * @param[out] size -- size of return string
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int epoch_to_icd_format(time_t rawtime, char *formated_time, size_t size);

/**
 * string the extension of file from file name
 *
 * @param[in/out] fname - epoch time
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
void strip_ext(char *fname);
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
int mklink(const char file[PATH_MAX], char destination[DESTINATION_LENGTH]);

/**
 * Function to create directory recursively 
 *
 * @param[in]   dir - source  
 *
 * @return    None
 */
void _mkdir(const char *dir);
/**
 * trim_trailing_undersore
 *
 * @param[in] v_string 
 * @param[in] len 
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int trim_trailing_undersore(char * v_string, int len);
/**
 * add_trailing_underscore
 *
 * @param[in] v_string 
 * @param[in] len 
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int add_trailing_underscore(char * v_string, int len);
#endif
