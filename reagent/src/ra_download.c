/**
 * @file    ra_download.c
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
#include <syslog.h>
#include <sys/statvfs.h>

#include "groom_util.h"
#include "reagent_config.h"
#include "lvdat_metadata.h"
#include "ra_data_search.h"
#include "ra_download.h"
#include "coc.h"
#include "ra_util.h"
#include "vradump.h"

#define  COMMAND_LEN_MAX	256	// a generic lengh for a command

// global variables 
static char  destination_dir[DESTINATION_LENGTH] = {'\0'};
static char  metadata_file[DESTINATION_LENGTH]   = {'\0'};
static char  temphash_file[DESTINATION_LENGTH]   = {'\0'};
static FILE *meta_fp                             = NULL;
static FILE *temp_hash_fp                        = NULL;

static int fps                  = 0;
static int hres                 = 0;
static int vres                 = 0;
static int compression_table[4] = {10,20,30,50};

static camdef_t     *p_camdef;
static media_info_t *p_minfo;

// global value to store the time offset obtained from the chunk.
static double starttimeoffset = 0;
enum
{
    e_download = 1,
    e_playback = 2
};

enum
{
    e_chm = 1,
    e_rssd =2
};

static int     radld_verbose  = 0;
uint8_t req_type = e_download;

/**
 * get_locoid
 *
 * @param[in] None   
 *
 * @return    loco_id
 */
char * get_locoid()
{   
    return (p_minfo->runtime.loco_id);
}

/**
 * get_rec_serial
 *
 * @param[in] None   
 *
 * @return    rec_ser_num
 */
char* get_rec_serial()
{
    return (p_minfo->rec_ser_num);
}

/**
 * cleanup file pointers
 *
 * @param[in] None   
 *
 * @return    None
 */
void cleanup()
{
    if (meta_fp != NULL)
    {
        fclose(meta_fp);
        meta_fp  = NULL;
    }

    if (temp_hash_fp != NULL)
    {
        fclose(temp_hash_fp);
        temp_hash_fp = NULL;
    }
    return;
}

/**
* Function  to get the time offset from the chunk header
*
* @param[in] p - input file
* @param[out] timeoffset from chunk header
*
* @return    EXIT_SUCCESS - On Success
*            EXIT_FAILURE - On Failure
*/
static int get_timeoffset_from_chunk_header(const char* chunk, bool isfirstchunk,uint64_t *offset,bool isplayback)
{
	vradump_file_pfx_t  *file_pfx = NULL;
	meta_time_memory_t  *timeinfo = NULL;
	char                 buffer[2048 + MAX_KEY_LENGTH + 4] = { '\0' }; // 4 additional bytes
	int                  bytes_read = 0;
	int                  rc = EXIT_FAILURE;
	FILE                *fp = NULL;

	if (chunk == NULL)
	{
		syslog(LOG_ERR, "%s: Invalid chunk name", __func__);
		return (EXIT_FAILURE);
	}

	fp = fopen(chunk, "rb");
	if (fp == NULL)
	{
		syslog(LOG_ERR, "%s: Unable to open chunk %s", __func__, chunk);
		return (rc);
	}

	bytes_read = fread(buffer, sizeof(char), sizeof(vradump_file_pfx_t), fp);
	fclose(fp);

	if (bytes_read > 0)
	{
		// cast the read bytes to header structure 
		file_pfx = (vradump_file_pfx_t *) &buffer[0];
		timeinfo = (meta_time_memory_t *) &file_pfx->subpfx_1.current_tm;

		if (strlen(timeinfo->time_src) > 0)	// a valid time source
		{
			syslog(LOG_NOTICE, "%s: A valid time info is found in the chunk. time %d, offset %ld", __func__, timeinfo->linux_time_sec_m, timeinfo->timeoffset_sec_m);
			//For export download from Wabtec Video client and URL query download we need to extract 
			//and give offset information as a part of xml which will be used by wabtec video player
			//for playback request directly from the wabtec video client offset information is not needed as xml 
			//is not geneated for playback request
			if(isfirstchunk)
			{
				if(!isplayback)
				{
					starttimeoffset = (double) timeinfo->timeoffset_sec_m;
					*offset = starttimeoffset ;
				}
				else
				{
					*offset = (uint64_t)timeinfo->timeoffset_sec_m;
				}
			}
			else
			{
				*offset = timeinfo->timeoffset_sec_m;
			}
		} 
		else   // If no valid time info is available then use the current offset.
		{
			syslog(LOG_NOTICE, "%s: A valid time info is not found in the chunk. Use current time offset.", __func__);
			if(isfirstchunk)
			{
				starttimeoffset = (double)get_timeoffset();
				*offset         = starttimeoffset;
			}
			else
			{
				*offset       = (uint64_t)get_timeoffset();
			}
			     
		}

		rc = EXIT_SUCCESS;
	}

	return (rc);
}

/**
 * Function to initiate the metadata file
 *
 * @param[in] fp   
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
FILE* init_meta_file(char * destination, int len)
{
    if (destination == NULL || len <= 0)
    {
        return NULL;
    }

    FILE *fp = NULL;

    memset(destination_dir,0,sizeof(destination_dir));
    memset(metadata_file,0,sizeof(metadata_file));
    memset(temphash_file,0,sizeof(temphash_file));
 
    strncpy(destination_dir,destination,len);
    strncpy(metadata_file,destination,len);
    strncpy(temphash_file,destination,len);
    strcat(metadata_file,META_FILE);
    strcat(temphash_file,TEMP_HASH);

#ifdef DEBUG
    syslog(LOG_INFO, "%s: DEST INFO : %s, META: %s, THASH: %s", __func__, destination_dir,metadata_file,temphash_file );
#endif

    if (radld_verbose)
    {
        fprintf(stderr, "DEST INFO : %s\nMETA: %s\nTHASH: %s\n",destination_dir,metadata_file,temphash_file );
    }

    fp = fopen(metadata_file, "w"); 

    if (fp == NULL)
    {
        syslog(LOG_ERR,"%s: Failed to open metafile errno [%d]", __func__, errno);
        return NULL;
    }

    temp_hash_fp = fopen(temphash_file, "w");

    if (temp_hash_fp == NULL) 
    {
        syslog(LOG_ERR,"%s: Failed to open hash file errno [%d]", __func__, errno);
        cleanup();
        fclose(fp);
        return (NULL);
    }

    fprintf(fp,"%s", metadata_start);
    fprintf(fp, "%s", metadata_segments_start);
    return (fp);
}

/**
 * Function to write the video segment details in 
 * metadata file 
 *
 * @param[in] fp 
 * @param[in] p_segment   
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int write_meta_chunk(FILE *fp, lvdat_chunk_t *p)
{
    if (fp == NULL)
    {
        return (EXIT_FAILURE);
    }
    if (radld_verbose)
    {
        fprintf(stderr, metadata_video_chunk,p->name,p->signature);
    }

    fprintf(fp, metadata_video_chunk, p->name, p->signature);
    return(EXIT_SUCCESS);
}

/**
 * Function to write the video segment details in 
 * metadata file 
 *
 * @param[in] fp 
 * @param[in] p_segment   
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int write_meta_segment(FILE *fp, segment_t *p_segment)
{
    if (fp == NULL)
    {
        return (EXIT_FAILURE);
    }
    if (radld_verbose)
    {
        fprintf(stderr, metadata_video_segment,p_segment->start_time,p_segment->end_time);
    }

    fprintf(fp,     metadata_video_segment,p_segment->start_time,p_segment->end_time);
    return(EXIT_SUCCESS);
}

/**
 * Function to get metadata from drive
 *
 * @param[in] fp - file pointer to main metafile
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int update_download_validation(FILE *fp, drData_t * data)
{
    if (fp == NULL)
    {
        return (EXIT_FAILURE);
    }
    uint8_t timeStr[TIME_MAX_STRING_LENGTH] = {0};
    int i = 0;

    fprintf(fp, "%s", metadata_corrupt_video_start);
    
    for (i = 0; i < data->found_chunks; i++)
    {
        if (data->chunk[i].corrupt == true)
        {
            epoch_to_string(data->chunk[i].epoch_time, &timeStr[0]);
            fprintf(fp, metadata_corrupt_time_range, timeStr, &timeStr[0] + (data->chunk[i].duration / 1000000));
        }
    }
    fprintf(fp, "%s", metadata_corrupt_video_end);
    
    return (EXIT_SUCCESS);
}

/**
 * Function to get metadata from drive
 *
 * @param[in] fp - filepointer to main metafile
 * @param[in] downloadMessage
 * @param[in] drive - 1=CHM,2=SSD
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int get_metadata_from_drive(FILE *fp, va_download_t *downloadMessage, uint8_t drive)
{
   // if (len < PATH_MAX)
     //   return (EXIT_FAILURE);
    char  command_templ[]  = "/opt/reagent/bin/metacollector -l %s -d %s -k METADATA -s %s -e %s -g true -v true -t true -i true %s -p -h";
    char  command[1024]    = {'\0'};
    char  template[]       = "/tmp/metadataXXXXXX";
    char  fname[PATH_MAX]  = {0};
    char  s_time[24]       = {'\0'};
    char  e_time[24]       = {'\0'};
    int   fd               = 0;
    FILE *m_fp             = NULL;
    char  buf[512]         = {'\0'};
    int   rc               = 1;
    int   match_flag       = 0;

    //generate tempXXXXX file
    strcpy(fname, template);
   
    fd = mkstemp(fname);

    if (fd <= 0 )
    {
        return (EXIT_FAILURE);
    }
    close(fd);

    memset(s_time, '\0', 24);
    epoch_to_icd_format((time_t)downloadMessage->payload.start_time,s_time,23);
    
    memset(e_time, '\0', 24);
    epoch_to_icd_format((time_t)downloadMessage->payload.end_time,e_time,23);

    snprintf(command,1024,command_templ,get_locoid(),(drive == 1)?"CHM":"SSD",s_time,e_time,fname);

    if(radld_verbose)
    {
        fprintf(stderr, "%s\n",command );
    }

    //run metacollector to get metadata from drive
    // get output of it into a file.
    rc = system(command);

    if (rc != EXIT_SUCCESS)
    {
        return(EXIT_FAILURE);
    }

    // open output file to read
    m_fp = fopen(fname, "r+");

    if (m_fp == NULL) 
    {
        return (EXIT_FAILURE);
    }

    // read output file and dump into main metadata file
    match_flag = 0;
    rc         = 1;

    while (rc != 0 && rc != EOF)
    {
        memset(buf, 0, 512);
        rc = fread(buf, sizeof(buf)-1, 1, m_fp);

        if (rc >= 0 && strlen(buf) > 0 )
        {   
            if (match_flag == 0)
            {
                match_flag = remove_substr(buf,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
            }
            fprintf(fp,"%s",buf);
        }
    }
    fclose(m_fp);
    m_fp = NULL;
    if (remove(fname) != 0) // remove temp file
    {
        syslog(LOG_WARNING, "%s: Unable to remove (%s).\n", __func__, fname);
    }

    return (EXIT_SUCCESS);
}

/**
 * Function to get meta-data based on start and end date provide
 * creates a file to put data in. 
 *
 * @param[in] fp - filepointer to main metafile
 * @param[in] downloadMessage
 * @param[in] drive - 1=CHM,2=SSD
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int insert_maindata(FILE *fp, drData_t * data, va_download_t *downloadMessage, uint8_t drive, int video_clip_count)
{
    int i = 0;
    uint8_t timeStr[TIME_MAX_STRING_LENGTH] = {0};

    if (fp == NULL)
    {
        syslog(LOG_ERR, "%s: Invalid Parameter to function.", __func__);
        return (EXIT_FAILURE);
    }

    unsigned char hex_sig[MAX_KEY_LENGTH]   = {'\0'};      //buffer for digital signature as hex
    char          sig[2*MAX_KEY_LENGTH + 1] = {'\0'};             // buffer to convert hex signature to hex_string
    maindata_t    maindata;
    long          length                    = 0;
    char          dest_file[PATH_MAX]       = {'\0'};

    memset(hex_sig, 0, sizeof(hex_sig));
    memset(sig, 0, sizeof(sig));
    memset(dest_file, '\0', PATH_MAX);
	
    //end tag for video segments
    memset(&maindata, 0, sizeof(maindata));
    fprintf(fp, "%s", metadata_segments_end);
    
    //populating rec_serial 
    strncpy(maindata.serial, get_rec_serial(), 12);
    
    //populating locoid
    
    strncpy(maindata.locoid, get_locoid(), sizeof(maindata.locoid) - 1);
    // populating target camera name

    memcpy(maindata.name, data->name, sizeof(maindata.name));
    trim_trailing_undersore(maindata.name, CAMERA_NAME_LENGTH + 1);

    memcpy(maindata.model, data->model, sizeof(data->model));
    // populating total count of video chunks in this download
    maindata.segment_count = video_clip_count;
    // populating start time of downloaded data
    epoch_to_string((time_t)downloadMessage->payload.start_time, &timeStr[0]);
    memcpy(maindata.stattime, timeStr, sizeof(maindata.stattime));
    // populating end time of downloaded data
    epoch_to_string((time_t)downloadMessage->payload.end_time, &timeStr[0]);
    memcpy(maindata.endtime, timeStr, sizeof(maindata.endtime));

    maindata.stattime[sizeof(maindata.stattime) - 1] = '\0';
    maindata.endtime[sizeof(maindata.endtime) - 1] = '\0';

    maindata.starttimeoffset = starttimeoffset;
    starttimeoffset = 0;
#ifdef DEBUG
	syslog(LOG_NOTICE, "%s: Time offset obtained from chunk %f", __func__, maindata.starttimeoffset);
#endif

    strcpy(maindata.gpsformat,"DMS");
    
    // get info from re_agent XML/configuration.
    p_minfo = raconf_get_media_info(0);

    if (p_minfo == NULL)
    {
        syslog(LOG_ERR, "%s: Failed to find current configurations %.12s ",__func__, maindata.name);        
        return (DOWNLOAD_INTERNAL_ERR);
    }
    else
    {
        // Update mount type.
        p_camdef = raconf_find_camera(p_minfo,maindata.name);

        if (p_camdef != NULL)
        {
            maindata.mount_type = atoi(p_camdef->mount_type);
#if DEBUG            
            syslog(LOG_DEBUG, "%s: Camera %s, mount_type %d", __func__, maindata.name, maindata.mount_type);
#endif            
        }
        else
        {
            maindata.mount_type = 0; //default value
            syslog(LOG_ERR, "%s: Unable to read config for Camera %s, default mount_type %d", __func__, maindata.name, maindata.mount_type);            
        }
    }

    //populating camera parameters from config
    snprintf(maindata.resolution, sizeof(maindata.resolution), "%dx%d", hres, vres);
    snprintf(maindata.compression, sizeof(maindata.compression), "%d",compression_table[(data->compression%4)]);
    snprintf(maindata.frame_rate, sizeof(maindata.frame_rate), "%d", fps);
   
    //Calculate final Digital Signature
    fclose(temp_hash_fp);

	temp_hash_fp = NULL;
    length       = calc_HMAC(temphash_file,HMACKEY,strlen(HMACKEY),hex_sig,MAX_KEY_LENGTH);

    for(i=0;i<length;i++)
    {
        sprintf(&sig[i*2], "%02X", hex_sig[i]);
    }

    if(radld_verbose)
    {
        fprintf(stderr, "Hash of %s:%s\n", temphash_file, sig);
    }

    memcpy(maindata.signature,sig,strlen(HMACKEY));

    // removing temp hash file after calculations
    if (remove(temphash_file) != 0)
    {
        syslog(LOG_WARNING, "%s: Failed to remove %s at line (%d).\n", __func__, temphash_file, __LINE__);
    }

    //write to metadata file
    fprintf(fp, metadata_maindata,maindata.serial, maindata.locoid, 
            maindata.name, maindata.model, maindata.segment_count, maindata.stattime, maindata.endtime,
            maindata.starttimeoffset, maindata.gpsformat, maindata.mount_type,
            maindata.resolution, maindata.compression, maindata.frame_rate, maindata.signature);

    if (radld_verbose)
    {
        fprintf(stderr, metadata_maindata, maindata.serial, maindata.locoid, 
                maindata.name, maindata.model, maindata.segment_count, maindata.stattime, maindata.endtime,
                maindata.starttimeoffset, maindata.gpsformat, maindata.mount_type,
                maindata.resolution, maindata.compression, maindata.frame_rate, maindata.signature);
    }

    /* Not needed in new lvdat design 02/18/2021
    if (EXIT_SUCCESS !=  update_download_validation(fp,data))
    {
        syslog(LOG_NOTICE, "Failed to retrieve download validation data");
    }*/

    // get remaining data from drive
	/* adjust the start and end time base on found video data*/
    downloadMessage->payload.start_time  = data->chunk[0].epoch_time;
    downloadMessage->payload.end_time    = data->chunk[video_clip_count - 1].epoch_time;
    downloadMessage->payload.end_time   += (uint64_t)(data->chunk[video_clip_count - 1].duration/1000000);

    if (EXIT_SUCCESS !=  get_metadata_from_drive(fp,downloadMessage,drive))
    {
        syslog(LOG_ERR, "%s: Failed to retrieve metadata from drive", __func__);
    }

    fprintf(fp,"%s", metadata_end);
    fclose(meta_fp);
    meta_fp = NULL;
    
    //Finally, encrypt the metadata.xml
    strncpy(dest_file, destination_dir,PATH_MAX);
    strcat(dest_file, "metadata.dat");

    if (EXIT_FAILURE == encrypt_file(metadata_file, dest_file))
    {
        if(remove(metadata_file) != 0)//capturing return here, as it may fail
        {
            syslog(LOG_WARNING, "%s: Unable to remove file (%s) (%d) at line (%d).\n", __func__, metadata_file, errno, __LINE__);
        }
        syslog(LOG_ERR, "%s: Failed to encrypt metadata.xml", __func__);
        return (DOWNLOAD_INTERNAL_ERR);
    }

    // remove un-encrypted file.
    if(remove(metadata_file) != 0)//capturing return here, as it may fail
    {
        syslog(LOG_WARNING, "%s: Failed to remove (%s) (%d) at line (%d).\n", __func__, metadata_file, errno, __LINE__);
    }

    return (DOWNLOAD_OK_FULL);
}

/**
 * Cleans the download Stage directory 
 *
 * @param[in] path
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int clean_download_dir(char * path)
{
    int rc = EXIT_FAILURE;
    int len = 0;
    char command[PATH_MAX] = {'\0'};
    const char *extStr_lvdat = "/*.lvdat";
    const char *extStr = "/*";

    if (path == NULL)
    {
        return EXIT_FAILURE;
    }

    // TODO: Moving forward, the VIDEO_DOWNLOAD_STAGE_LOCATION should not be hard-coded by a #define.
    // This path will come to RA from the host application via the PTC IPC mechanism, along with a backup
    // location (VIDEO_DOWNLOAD_BACKUP_STAGE_LOCATION) if the default staging directory is not present
	// to be updated via Story 166236.

    if ((strcmp(path, VIDEO_DOWNLOAD_STAGE_LOCATION)        == 0) ||
        (strcmp(path, VIDEO_DOWNLOAD_BACKUP_STAGE_LOCATION) == 0))   // If the RSSD is not present, the staging directory will be "/ldars/vdownload/"
    {
        // we can blow out the whole directory in this case
        strcpy(command, "rm -f ");
        if ((strlen(path) + strlen(extStr)) > ((PATH_MAX - 1) - strlen(command)))
        {
            syslog(LOG_ERR, "%s: String length of %s exceeds %d.\n", __func__, path, PATH_MAX);
            return EXIT_FAILURE;
        }
        strcat(command, path);
        strcat(command, extStr);
    }
    else
    {
        // we cannot blow out the whole directory
        // only remove *.lvdat files
        strcpy(command, "rm -f ");
        if ((strlen(path) + strlen(extStr_lvdat)) > ((PATH_MAX - 1) - strlen(command)))
        {
            syslog(LOG_ERR, "%s: String length of %s exceeds %d.\n", __func__, path, PATH_MAX);
            return EXIT_FAILURE;
        }
        strcat(command, path);
        strcat(command, extStr_lvdat);
    }

    
    rc = system(command);

    if (rc != EXIT_SUCCESS)
    {
        syslog(LOG_ERR, "%s: Unable to clean download directory [%s]", __func__, path);
        return (EXIT_FAILURE);
    }

    return (EXIT_SUCCESS);
}

/**
 * Create final zip file from file available in stage directory 
 *
 * @param[in] None
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int zip_files(const char *file_name, uint64_t *file_size, bool encrypt)
{
    if (file_name == NULL)
    {
        return (DOWNLOAD_INTERNAL_ERR);
    }
    syslog(LOG_ERR, "%s: destination_dir=%s\n", __func__, destination_dir);
    char zip_file[PATH_MAX] = {0};
    char zip[256] = {0};
    char *dest_dir = destination_dir;
    int rc = EXIT_FAILURE;
    
    memset(zip_file,0,sizeof(zip_file));    
    if(strlen(file_name) > (PATH_MAX-strlen(".zip")-1))
	    return (DOWNLOAD_INTERNAL_ERR);
    memcpy(zip_file,file_name,strlen(file_name));
    strcat(zip_file,".zip");

    memset(zip,0,sizeof(zip));
    strcpy(zip,"/opt/reagent/bin/zip");

    //build a shell zip command and execute it.
    char command[BUFF_SIZE_BIG] = {'\0'};
    memset(command, '\0', sizeof(command));
    strcpy(command,zip);
    strcat(command, " -0 -m -j -q "); //-0 zero compression | -m   move into zipfile (delete OS file) | -j   junk (don't record) directory names | -q   quiet operation (don't output)
    strcat(command, zip_file);
    strcat(command, " ");
    strcat(command, destination_dir);
    strcat(command,"* > /dev/null 2>&1");
    rc = system(command);
    if (rc != EXIT_SUCCESS)
    {
        fprintf(stderr, "%s: zip command failed - exit code [%d]\n", __func__, rc);
        syslog(LOG_ERR, "%s: zip command failed - exit code [%d]", __func__, rc);
        clean_download_dir(dest_dir);
        return (DOWNLOAD_INTERNAL_ERR);
    }

    if (radld_verbose)
        fprintf(stderr,"\n%s\n",command );
    
    if (encrypt == true)
    {
        if (EXIT_FAILURE == encrypt_file(zip_file,file_name))
        {
            fprintf(stderr, "%s: Unable to encrypt zip file %s.\n", __func__, zip_file);
            syslog(LOG_ERR, "%s: Unable to encrypt zip file %s.", __func__, zip_file);
            clean_download_dir(dest_dir);
            return (DOWNLOAD_INTERNAL_ERR);
        }
        // remove un-encrypted file.
        if(remove(zip_file) != 0)
        {
            syslog(LOG_WARNING, "%s: Unable to remove file %s.\n", __func__, zip_file);
        }
    }

    if ( EXIT_SUCCESS !=  get_file_size(file_name, file_size))
    {
        fprintf(stderr, "%s: Failed to get file size\n",__func__ );
        syslog(LOG_ERR, "%s: Failed to get file size", __func__);
    }

    return (DOWNLOAD_OK_FULL);
}

char *add_cumulative_sig(const char *p, FILE *fp)
{
    if (p == NULL || fp == NULL)
    {
        fprintf(stderr, "%s[%d]: %s\n",__func__,__LINE__,p );
        return NULL;
    }

    unsigned char hex_sig[MAX_KEY_LENGTH + 1] = {'\0'};
    static char   sig[2 * MAX_KEY_LENGTH + 1] = {'\0'};
    int           i                           = 0;

    memset(hex_sig, 0, sizeof(hex_sig));
    memset(sig, 0, sizeof(sig));
    get_stored_sig(p, hex_sig, MAX_KEY_LENGTH, &fps, &hres, &vres);

    for (i = 0; i < strlen(HMACKEY)/2; i++)
    {
        sprintf(&sig[i*2], "%02X", hex_sig[i]);
    }

    if (radld_verbose)
    {
        fprintf(stderr, "Hash of %s:%s\n", p, sig);
    }

    //writing digital signature of of each segment into a temp file 
    //to calculate final digital signature
    fprintf(fp, "%s", sig);
    return (sig);
}

int prepare_playback(drData_t *data, int drive, char * destination,data_range_t *drange,int found_chunk_count)
{
    static char tp[PATH_MAX] = {0};
    int i = 0;
    int first_chunk = 0;
    int total_found_chunks = 0;
    uint64_t first_chunk_offset = 0;
    uint64_t last_chunk_offset = 0;

    if (data->found_chunks <= 0)
    {
        syslog(LOG_ERR,"%s: Incorrect Chunk count ",__func__);
        return EXIT_FAILURE;
    }

    total_found_chunks = data->found_chunks ;

    for (i = 0; i < data->found_chunks; ++i)
    {   
        memset(tp,0,sizeof(tp));
        
        make_filepath(drive - 1, data->name, data->mac, &(data->chunk[i]), tp, sizeof(tp));

        if (tp[0] == '\0')
        {
            continue;
        }

    	if (first_chunk == 0)
    	{
    		// Extract the timeoffset from the first chunk used for this time range. This time offset will be used 
    		// as the start time offset in the metadata.
    		// Note that the make_filepath() function sorts all the chunks that we use so that the earliest time chunk is the first one we use.
		// Use this function to extract time offset of first file and last file and re-calculate start time and end time if offset exists
    		get_timeoffset_from_chunk_header((const char*)tp,true,&first_chunk_offset,true);
    		first_chunk = 1;
    	}

	    if(i == total_found_chunks-1)
	    {
		   get_timeoffset_from_chunk_header((const char*)tp,false,&last_chunk_offset,true);
	    }

	
	    if (drange != NULL)
	    {
		    drange->start_time = data->chunk[0].epoch_time - first_chunk_offset;
		    drange->end_time = data->chunk[found_chunk_count - 1].epoch_time + (data->chunk[found_chunk_count - 1].duration/micro_secs_per_second + 1)-last_chunk_offset;
#ifdef DEBUG
		    syslog(LOG_INFO, "%s: Data range start time %d, Data range end time %d", __func__, drange->start_time, drange->end_time);
#endif            
	     }


        if (radld_verbose)
        {
            fprintf(stderr, "chunk [%d]: %s gap %d\n",i,tp,data->chunk[i].gap );
        }   
        mklink(tp,destination);
    }
    return (EXIT_SUCCESS);
}

int insert_chunk_metadata(FILE *fp, drData_t *data, int drive, char *destination)
{
    char tp[PATH_MAX] = {0};
    char tq[PATH_MAX] = {0};
    segment_t segment;
    lvdat_chunk_t lvdat_chunk;
    uint64_t s = data->chunk[0].epoch_time;
    int i = 0;
    int cc = 0;
    char *sig = NULL;
    char *p = NULL;
    int rc = EXIT_FAILURE;
    struct stat sb;
    struct statvfs statv;
    double total_size = 0; // size in bytes of chunk
    double available_storage = 0;
    int first_chunk = 0;
    int total_found_chunks = 0;
    uint64_t first_chunk_offset = 0;
    uint64_t last_chunk_offset = 0;
    uint8_t timeStr[TIME_MAX_STRING_LENGTH] = {0};

    if (data->found_chunks <= 0)
    {
	    syslog(LOG_ERR, "%s: Incorrect Chunk count ", __func__);
	    return (DOWNLOAD_INTERNAL_ERR);
    }
    else
    {
	  total_found_chunks = data->found_chunks ;
    }

    if (statvfs(destination, &statv) != 0)
    {
        fprintf(stderr, "Unable to stat destination");
        syslog(LOG_ERR, "%s: Unable to stat destination", __func__);
    }
    else
    {
        available_storage = (double)((long)statv.f_bfree*(long)statv.f_bsize) / (1024 * 1024);
#ifdef DEBUG
        fprintf(stderr, "Available storage at %s [%f]", destination, available_storage);
        syslog(LOG_ERR, "%s: Available storage at %s [%f]", __func__, destination, available_storage);
#endif
    }

    memset(&segment,0,sizeof(segment));
    memset(&lvdat_chunk,0,sizeof(lvdat_chunk));

    // go through and check the files for size
    for (i = 0; i < data->found_chunks; ++i)
    {
        memset(tp, 0, sizeof(tp));
        rc = make_filepath(drive - 1, data->name, data->mac, &(data->chunk[i]), tp, sizeof(tp));
        if (rc == EXIT_FAILURE)
        {
            continue;
        }
        if (stat(tp, &sb) != 0)
        {
            fprintf(stderr, "Unable to stat %s", tp);
            syslog(LOG_ERR, "%s: Unable to stat %s", __func__, tp);
        }

        total_size += (double)sb.st_size;
    }

    if ((total_size / (1024 * 1024)) >= (available_storage * MAX_DOWNLOAD_PERCENTAGE))
    {
        fprintf(stderr, "Unable to complete download. Available storage [%f] Current Download Size [%f]", (available_storage * .6), (total_size / (1024 * 1024)));
        return (DOWNLOAD_SIZE_ERR);
    }

	// Once we have determined that size is OK for all the chunks proceed with creating the download file.
    for (i = 0; i < data->found_chunks; ++i)
    {   
        memset(tp,0,sizeof(tp));
        memset(tq,0,sizeof(tq));
        rc = make_filepath(drive-1,data->name,data->mac,&(data->chunk[i]),tp, sizeof(tp));
        
    	if (rc == EXIT_FAILURE)
    	{
    		continue;
    	}
#ifdef DEBUG
    	if (strlen(tp) != 0)
    	{
    		syslog(LOG_NOTICE, "%s: This chunk is to be used for download %s", __func__, tp);
    	}
#endif
    	if (first_chunk == 0)
    	{
    		// Extract the timeoffset from the first chunk used for this time range. This time offset will be used 
    		// as the start time offset in the metadata.
    		// Note that the make_filepath() function sorts all the chunks that we use so that the earliest time chunk is the first one we use.
    		get_timeoffset_from_chunk_header((const char*)tp,true,&first_chunk_offset,false);
    		first_chunk = 1;
    	}

	
        if (radld_verbose)
        {
            fprintf(stderr, "chunk [%d]: %s gap %d\n", i, tp, data->chunk[i].gap);
        }
        
        if (cc++ == 0)
        {
	        s -= first_chunk_offset ;
            fprintf(fp, "%s", metadata_video_segment_start);
            epoch_to_string((time_t)s, &timeStr[0]);
            strcpy(segment.start_time, timeStr);
        }

        // update temp hash file with hash of each chunk to calculate cumulative hash
        sig = add_cumulative_sig(tp,temp_hash_fp);
        p   = strrchr(tp,'/') + 1;

        memcpy(lvdat_chunk.name, p, sizeof(lvdat_chunk.name));
        memcpy(lvdat_chunk.signature, sig, strlen(HMACKEY));
        write_meta_chunk(fp, &lvdat_chunk);

        if (data->chunk[i].gap == true || i + 1 >= data->found_chunks)
        {
            
	    get_timeoffset_from_chunk_header((const char*)tp,false,&last_chunk_offset,false);
	    s  = data->chunk[i].epoch_time;
	        s -= last_chunk_offset;
            s += (uint64_t)(data->chunk[i].duration/1000000);

            epoch_to_string((time_t)s, &timeStr[0]);
            strcpy(segment.end_time, timeStr);
            write_meta_segment(fp, &segment);

            if ((i + 1) < data->found_chunks)
            {
		get_timeoffset_from_chunk_header((const char*)tp,false,&first_chunk_offset,false);
                s = data->chunk[i + 1].epoch_time;
                s -= first_chunk_offset;
                fprintf(fp, "%s", metadata_video_segment_start);
                epoch_to_string((time_t)s, &timeStr[0]);
                strcpy(segment.start_time, timeStr);
            }
        }

        // copy chunk to destination one by one
        snprintf(tq, sizeof(tq) - 1, "%s/%s", destination, p);
        if (copy_file(tp, tq) != EXIT_SUCCESS)
        {
            syslog(LOG_ERR, "%s: Failed to copy", __func__);
            return (DOWNLOAD_INTERNAL_ERR);
        }
        
    }

#ifdef DEBUG
    syslog(LOG_INFO, "%s: Chunk count %d", __func__, cc);
#endif
    
    if (cc <= 0)
    {
        syslog(LOG_ERR, "%s: Chunk count <= 0 [%d]", __func__, cc);
        return (DOWNLOAD_FAILURE);
    }

    return (DOWNLOAD_OK_FULL);
}

/**
 * Validate the destination directory  
 *
 * @param[in] destination - path
 * @param[in] len - path length 
 * @param[in] type - request type download or playback
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int validate_destination(char * destination, int len, uint8_t type)
{
    struct stat    sb;
    struct statvfs statv;

    double available_storage;
    float  storage_limit      = DOWNLOAD_MIN_STORAGE_LIMIT;

    if (stat(destination, &sb) == 0 && S_ISDIR(sb.st_mode))
    {

        if (EXIT_SUCCESS != clean_download_dir(destination))
        {
            syslog(LOG_ERR, "%s: Can't clean download stage directory %s.", __func__, destination);
            return (EXIT_FAILURE);
        }

        if (statvfs(destination, &statv) != 0) 
        {
            // error happens, just quits here
            syslog(LOG_ERR, "%s: Unable to get destination statistics", __func__);            
            return (EXIT_FAILURE);
        }

        available_storage = (double)((long)statv.f_bfree*(long)statv.f_bsize)/(1024*1024);
#ifdef DEBUG
        syslog(LOG_INFO, "%s: Available storage in destination %f", __func__, available_storage);       
#endif        

        if (radld_verbose)
        {
            fprintf(stderr, "INFO: Free storage available: %f\n",available_storage );
        }
        
        if (type == e_playback)
        {
            storage_limit = PLAYBACK_MIN_STORAGE_LIMIT;
        }

        if (available_storage < storage_limit)
        {
            syslog(LOG_ERR, "%s: Available storage %f is less than storage limit %f", __func__, available_storage, storage_limit);                        
            return (EXIT_FAILURE);
        }

        return (EXIT_SUCCESS);
    }
    else
    {
        return (EXIT_FAILURE);
    }
}

/**
 * main function to do the video download
 *
 * @param[in]   va_download_t - download struct from download struct 
 * @param[out]  file_size     - file size in KB
 * @param[out]  status        - status of download successful or failure
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int perform_download(va_download_t *downloadMessage, uint8_t type,uint8_t drive, uint64_t *file_size, uint8_t *status, data_range_t *drange)
{
    //clear the flag
    req_type = type;

    char destination[PATH_MAX] = {'\0'};
    char command[COMMAND_LEN_MAX] = {'\0'};

    drData_t data;

    int   found_chunk_count = 0;
    int   rc                = EXIT_FAILURE;
    char *p                 = NULL;
    long current_offset     = 0;
    
    memset(&data, 0, sizeof(data));

    *file_size = 0;
    *status    = DOWNLOAD_FAILURE;

    // All requests are done via ADJUSTED time
    // UNADJUST time for file system search
    
    syslog(LOG_NOTICE,"%s: %s requested: Camera %.12s, drive %s, start_time %u, end_time %u", __func__,
           req_type == 1? "DOWNLOAD":"PLAYBACK",
           downloadMessage->payload.camera,
           drive == e_chm? "CHM":"RSSD",
           downloadMessage->payload.start_time,
           downloadMessage->payload.end_time);

    if (radld_verbose)
    {
        fprintf(stderr,"TIME INFO: offset: %ld Seconds\n",get_timeoffset());
    }

    strncpy(destination, downloadMessage->payload.destination,DESTINATION_LENGTH);
#ifdef DEBUG
    syslog(LOG_INFO, "%s: download destination %s", __func__, destination);
#endif        
	
    if (req_type == e_playback)
    {
        //setup session directory
        _mkdir(destination);
        sprintf(command,"rm -rf %s/*",destination);
        rc = system(command);

        if (rc == EXIT_FAILURE)
        {
            syslog(LOG_ERR, "%s: System command '%s' failed.",__func__, command);            
            return (rc);
        }
            
        strcat(destination,"/");
    }
    else
    {
        if ((p = strrchr(destination, '/')) != NULL)
        {
            ++p;
            *p = '\0';
        }

        if (EXIT_FAILURE ==  validate_destination(destination,strlen(destination),req_type))
        {
            syslog(LOG_ERR,"%s: Not enough space available at %s", __func__, destination);
            *status = DOWNLOAD_INTERNAL_ERR;    // something happened that the directory is full
            return (EXIT_FAILURE);
        }
    }

    found_chunk_count = retrive_data(drive-1,downloadMessage->payload.camera,
                                     downloadMessage->payload.start_time,
                                     downloadMessage->payload.end_time,
                                     &data);
#ifdef DEBUG
    syslog(LOG_INFO, "%s: found chunk count %d", __func__, found_chunk_count);
#endif    

    if (radld_verbose)
    {
        fprintf(stderr, "%s: Total Chunk Found %d \n",__func__,found_chunk_count );
    }
    

    if (found_chunk_count < 1)
    {
        if (found_chunk_count == -1)
        {
            syslog(LOG_ERR, "%s: Requested Range is outside of available range. 416 Error.", __func__);
            *status = DOWNLOAD_RANGE_ERR;
            return (EXIT_FAILURE);
        }
        else
        {
            syslog(LOG_ERR, "%s: Failed to find video data. 404 Error", __func__);
            return (EXIT_FAILURE);
        }  
    }

    if (type == e_playback)
    {

        if (radld_verbose)
        {
            fprintf(stderr, "%s: It is playback request...\n",__func__ );
        }
			
        if (EXIT_SUCCESS != prepare_playback(&data,drive,destination,drange,found_chunk_count))
        {
            syslog(LOG_ERR, "%s: Failed to create metafile", __func__);
            return (EXIT_FAILURE);
        }
        return (EXIT_SUCCESS);
        // If playback request, no need to go further;
        // make symbolic links of found files to /tmp/playback/<session_id>/
        // rtsp server will automatically pick file based on session_id 
    }

     // get info from re_agent XML/configuration.
    p_minfo = raconf_get_media_info(0);

    if (p_minfo == NULL)
    {
        syslog(LOG_ERR, "%s: Failed to find current configurations %12s ",__func__, downloadMessage->payload.camera);
        *status = DOWNLOAD_INTERNAL_ERR;
        return (EXIT_FAILURE);
    }
	
    meta_fp = init_meta_file(destination,strlen(destination));

    if  (meta_fp == NULL)
    {
        cleanup();
        syslog(LOG_ERR, "%s: Failed to generate meta file.", __func__);
        *status = DOWNLOAD_INTERNAL_ERR;
        return (EXIT_FAILURE);
    }
    data.mac[MAC_LEN - 1] = '\0';
    rc = insert_chunk_metadata(meta_fp, &data, drive, destination);
    if (DOWNLOAD_OK_FULL != rc)
    {
        // transfer of files to download dir failed
        cleanup();
        syslog(LOG_ERR, "%s: Failed to create metafile", __func__);

        switch (rc)     // only 3 ways to fail from the function, set status accordingly
        {
        case DOWNLOAD_SIZE_ERR:
            *status = DOWNLOAD_SIZE_ERR;
            break;
        case DOWNLOAD_INTERNAL_ERR:
            *status = DOWNLOAD_INTERNAL_ERR;
            break;
        case DOWNLOAD_FAILURE:
        default:
            *status = DOWNLOAD_FAILURE;
            break;
        }
        
        return (EXIT_FAILURE);
    }
    
    rc = insert_maindata(meta_fp, &data, downloadMessage, drive, data.found_chunks);
    if (DOWNLOAD_OK_FULL != rc)
    {
        // transfer of metadata to download dir failed
        cleanup();
        syslog(LOG_ERR, "%s: Failed to add maindata to metafile", __func__);

        switch (rc)     // only 2 ways to fail from the function, set status accordingly
        {
        case DOWNLOAD_INTERNAL_ERR:
            *status = DOWNLOAD_INTERNAL_ERR;
            break;
        case DOWNLOAD_FAILURE:
        default:
            *status = DOWNLOAD_FAILURE;
            break;
        }

        return (EXIT_FAILURE);
    }

    rc = zip_files((const char *)downloadMessage->payload.destination, file_size, true);
    if (DOWNLOAD_OK_FULL != rc)
    {
        // transfer of files to download dir failed
        cleanup();
        syslog(LOG_ERR, "%s: Failed to zip files", __func__);

        switch (rc)     // only 2 ways to fail from the function, set status accordingly
        {
        case DOWNLOAD_INTERNAL_ERR:
            *status = DOWNLOAD_INTERNAL_ERR;
            break;
        case DOWNLOAD_FAILURE:
        default:
            *status = DOWNLOAD_FAILURE;
            break;
        }

        return (EXIT_FAILURE);
    }

    cleanup();

    //TODO partial download.......
    
    //*status = (uint8_t)2; //only for LNVR
    *status = (uint8_t) DOWNLOAD_OK_FULL;
    return (EXIT_SUCCESS);
}

/**
 * main function to do the video download
 *
 * @param[in]   va_download_t - download struct from download struct 
 * @param[out]  file_size     - file size in KB
 * @param[out]  status        - status of download successful or failure
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int do_download(va_download_t *downloadMessage, uint64_t *file_size, uint8_t *status)
{
    /* This request will come through the PTC API. It must be a download request. 
	 * first attempt a RSSD download
	 */
	int rc = EXIT_FAILURE;
	/* shared memory have camera names with trailing underscores. 
	let's add it first.*/
    add_trailing_underscore(downloadMessage->payload.camera,CAMERA_NAME_LENGTH);
	rc = perform_download(downloadMessage, (uint8_t)e_download, (uint8_t)e_rssd, file_size, status,NULL);
	
	if (rc != EXIT_SUCCESS)
	{
		// RSSD download was a failure, now attempt to do a download from CHM
		// whatever the status of the CHM search is, will be what is displayed to the user

		rc = perform_download(downloadMessage, (uint8_t)e_download, (uint8_t)e_chm, file_size, status, NULL);
	}	
    return (rc);
}

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
int get_snapshot(camera_snapshot_t *cam_snap, uint64_t *file_size, uint8_t *status)
{
    *file_size = 0;
    *status    = DOWNLOAD_FAILURE;

    int   rc                     = EXIT_FAILURE;
    int   rc1                    = EXIT_FAILURE;
    char *snap                   = NULL;
    char *snap_url               = NULL;
    char  command[BUFF_SIZE_BIG] = {'\0'};

    // get info from re_agent XML/configuration.
    p_minfo  = raconf_get_media_info(0);

    if (p_minfo == NULL)
    {
        syslog(LOG_ERR, "%s: Can't find current configurations %s ", __func__, cam_snap->camera);
        return (EXIT_FAILURE);
    }

    p_camdef = raconf_find_camera(p_minfo,cam_snap->camera);

    if (cam_snap == NULL || p_camdef == NULL)
    {
        syslog(LOG_ERR, "%s: Can't find camera in current configurations %s", __func__, cam_snap->camera);
        return (EXIT_FAILURE);
    }

    snap     = cam_snap->destination;
    snap_url = p_camdef->snapshot_url;

    if ((snap[0] == '\0') || (snap_url[0] == '\0'))
    {
        syslog(LOG_ERR, "%s: Invalid destination.\n", __func__);
        return (EXIT_FAILURE);
    }

    if (EXIT_SUCCESS != clean_download_dir(snap))
    {
        syslog(LOG_ERR, "%s: Can't clean download stage directory.\n", __func__);
        return (EXIT_FAILURE);
    }
    //wget --user=<> --password=<> -nd -r -O /mnt/removableSSD/download/vdownload/axis.jpg -A jpeg,jpg,bmp,gif,png http://10.10.9.100/onvif-cgi/jpg/image.cgi
    memset(command, '\0', sizeof(command));
    strcpy(command, "wget --user="); 
    strcat(command, USER);
    strcat(command, " --password=");
    strcat(command, PASS);
    strcat(command," -nd -r -O ");
    strcat(command, snap);
    strcat(command, " -A jpeg,jpg,bmp,gif,png ");
    strcat(command, snap_url);

    rc  = system(command);

    if (rc != EXIT_SUCCESS)
    {
        syslog(LOG_ERR, "%s: Snapshot download failed - exit code [%d]", __func__, rc);
        return (EXIT_FAILURE);
    }

    rc1 = get_file_size(snap, file_size);
    
    if (rc1 != EXIT_SUCCESS)
    {
        syslog(LOG_ERR, "%s: Failed to get file size.", __func__);
        return (EXIT_FAILURE);
    }

    *status = (uint8_t) DOWNLOAD_OK_FULL;
    return (EXIT_SUCCESS);
}
