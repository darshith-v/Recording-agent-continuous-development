/**
 * @file    ra_download.h
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

#ifndef RA_DOWNLOAD_H
#define RA_DOWNLOAD_H

#include <limits.h>

#include "video_api.h"
#include "common_defs.h"

#define MAX_CHUNK							 512
#define VIDEO_DOWNLOAD_STAGE_LOCATION        "/mnt/removableSSD/download/vdownload/"
#define VIDEO_DOWNLOAD_BACKUP_STAGE_LOCATION "/ldars/vdownload/"
#define MAX_DOWNLOAD_PERCENTAGE              .6      // 60% of download staging area

//============= Hard coded values ==================

#define META_FILE                           "metadata.xml"
#define TEMP_HASH                           "temp_hash"
#define TEMP_VIDEO_VALIDATION_FILE			"temp_video_validation.xml"
#define TEMP_METADATA_VALIDATION_FILE	    "temp_metadata_validation.xml"
#define USER								"reagent"
#define PASS								"WabtecRE2020"
//==================================================

#define DOWNLOAD_MIN_STORAGE_LIMIT					3200.00 //MBs
#define PLAYBACK_MIN_STORAGE_LIMIT					50.00 //MBs



typedef struct 
{
	uint32_t start_time;  
	uint32_t end_time;    
}data_range_t;

int check_download_range(uint32_t start_time, uint32_t end_time, const char* dir_path);

int do_download(va_download_t *downloadMessage, uint64_t *file_size, uint8_t *status);
int perform_download(va_download_t *downloadMessage, uint8_t type, uint8_t drive,uint64_t *file_size, uint8_t *status,data_range_t *drange);
/**
 * main function to do the video download
 *
 * @param[in]   camera_snapshot_t - snap struct 
 * @param[out]  file_size     - file size in KB
 * @param[out]  status        - status of download successful or failure
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int get_snapshot(camera_snapshot_t *cam_snap, uint64_t *file_size, uint8_t *status);
#endif
