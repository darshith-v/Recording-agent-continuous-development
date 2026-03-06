#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>

#include "path_util.h"
#include "metaintf.h"
#include "metaio.h"
#include "metachunk.h"
#include "metacoc.h"

#define METADATA_VERSION "1.0"

#define ONE_SEC_IN_NANO_SECS        1000000000
#define ONE_SEC_IN_MILI_SECS        1000

extern int verbose ;

/* This function generates a filesystem event by creating 
a empty file with same file name and duration 
as original video chunk for space_monitor. this function also
remove the generated file afterwards. */
static int generate_filesystem_event(char *path)
{
    int   rc       = EXIT_SUCCESS;
    FILE* file_ptr = NULL;

    if (path == NULL) {
        rc = -1;
    }
    else {
        file_ptr = fopen(path, "w");

        if (file_ptr) {
            if ((fclose(file_ptr)) != EXIT_SUCCESS) {
                syslog(LOG_ERR, "%s: fclose() failed to close file: %s with errno: %d", __func__, path, errno);
                rc = -1;
            }
            else {
                if (remove(path) != EXIT_SUCCESS) {
                    syslog(LOG_ERR, "%s: remove() failed with errno: %d", __func__, errno);
                    rc = -1;
                } // if (remove(path) != EXIT_SUCCESS)
            } // if if ((fclose(file_ptr)) == EXIT_SUCCESS)
        } // if (file_ptr)
    } // if (path != NULL)
    return rc;
}

static int prepare_subpath(metadata_io_chan *pchan, metadata_chunk_desc_t *pctx)
{
	int i, n = 0 ;
	char tmpbuf[MAXPATH] ;

	if (pchan) {
        for (i = 0; i < MAXDATALOC; i++) {
            if (pchan->data_loc[i].is_used) {      
                memset(&tmpbuf[0], 0, sizeof(tmpbuf));

    		    strcpy(tmpbuf, &(pchan->data_loc[i].datadir[0])) ;
        		n = strlen(tmpbuf) ;
        		if ((n>0) && (tmpbuf[n-1] != '/')) {
        			tmpbuf[n++] = '/' ;
        			tmpbuf[n] = '\0' ;
        		}
        		n = sprintf(&pchan->subpath[0], "%04d/%02d/%02d/%02d/",
        			pctx->prev_tm.tm_year+1900, pctx->prev_tm.tm_mon+1, pctx->prev_tm.tm_mday, pctx->prev_tm.tm_hour
        		) ;

        		strcat(&tmpbuf[0], &pchan->subpath[0] ) ;

        		n = mkdir_p(&tmpbuf[0]) ;
				if (n != 0) {
					syslog(LOG_ERR, "unable to create metadata recording directory '%s'\n",&tmpbuf[0]) ;
				}
           }
        }
	}
	return n ;
}

static int is_new_path_required(metadata_chunk_desc_t *pctx, struct tm tm)
{
    int required = 0;

    if ((pctx->session_just_started)  ||                // session just started
        ((pctx->prev_tm.tm_hour != tm.tm_hour) ||       // hour elapsed 
         (pctx->prev_tm.tm_mday != tm.tm_mday) ||       // day elapsed 
         (pctx->prev_tm.tm_mon != tm.tm_mon)   ||       // month elapsed 
         (pctx->prev_tm.tm_year != tm.tm_year)))        // year elapsed 
    {
        required = 1;
    }

    return required;
}

static int is_new_chunk_required(metadata_chunk_desc_t *pctx, struct tm tm)
{
    int required    = 0;
    int time_change = has_forward_time_change_occurred();

    if (time_change) {
        update_forward_time_change_status(false);
    }

    if ((is_new_path_required(pctx, tm)) ||     // new path needed
        (is_meta_record_reach_max(pctx)) ||     // max records reached
        (time_change)                    ||     // time change occurred
        (pctx->drive_status_updated)       )    // drive status updated  
    {
        required = 1;
    }
    
    return required;
}

/*
 * Write a header to current output file.
 * The header consists of an identifying string, followed
 * by a binary structure.
 */
static void metadata_chunk_header(FILE *out, struct timeval *start)
{
    int idx = 0;
    char temp_buffer[EVENT_NAME_LENGTH + 1] = {0};
    metadata_chunk_hdr_t hdr;
    metachunk_file_pfx_t file_pfx ;

    memset(&file_pfx, 0, sizeof(file_pfx)) ;

    hdr.start.tv_sec = htonl(start->tv_sec);
    hdr.start.tv_usec = htonl(start->tv_usec);

    sprintf(&(file_pfx.subpfx_1.shebang[0]), "#!metadata version [%s]\n", METADATA_VERSION) ;

    /* fill in the digital configuration mappings */
    for (idx = 0; idx < NUM_DIGITAL_INPUTS; ++idx) {
        if (get_digital_input_name(idx+1, (char *)&temp_buffer[0])) {
            file_pfx.subpfx_1.digital_mappings[idx].dig_num = idx;
            strncpy(&(file_pfx.subpfx_1.digital_mappings[idx].dig_name[0]), &temp_buffer[0], sizeof(file_pfx.subpfx_1.digital_mappings[idx].dig_name)-1);
        }
    }

    file_pfx.subpfx_2.max_records = sizeof(file_pfx.record_index) / sizeof(rcd_idx_t) ;

    if (fwrite((char*) &file_pfx, sizeof(file_pfx), 1, out) < 1) {
        perror("fwrite file pfx");
        return ;
    }

    if (fwrite((char*) &hdr, sizeof(hdr), 1, out) < 1) {
        perror("fwrite legacy chunk hdr");
        return ;
    }
}

int get_metadata_chunk_file(metadata_io_chan *pchan, metadata_desc_t *pdesc, FILE ** pfp)
{
    int rc = 0 ; 
    int idx, n = 0;
    FILE *new_fp = NULL ;
    struct tm tm ;
    time_t tsec ;    
    struct timeval start ;          
        
    long long unsigned int  t_start ;

    char tmpbuf[MAXPATH] ;
    char tb[MAXPATH] ;
    char newfname[MAXPATH] ;
    metadata_chunk_desc_t *pctx = &(pchan->m_chunk) ;

    if (pctx->deltat != 0) {        

        t_start = pdesc->ts.tv_sec ;
        t_start = t_start * ONE_SEC_IN_NANO_SECS + pdesc->ts.tv_nsec ;

        tsec = (time_t) (t_start + pctx->deltat) / ONE_SEC_IN_NANO_SECS ;

        // gmtime_r(&tsec, &tm) ;
        gmtime_r(&(pdesc->tod.tv_sec), &tm) ;

        pctx->chunk_duration_usecs= (unsigned int) ((t_start - pctx->t0) / ONE_SEC_IN_MILI_SECS) ;

	if (is_new_chunk_required(pctx, tm) == true) {
		pctx->session_just_started = 0 ;

		if (pctx->drive_status_updated) {
			pctx->drive_status_updated = 0 ;
		}

		/* construct full (currently staging) file name */
		for (idx = 0; idx < MAXDATALOC; idx++) {
                if (pchan->data_loc[idx].is_used) { 
                    if ((pctx->fp[idx] != NULL) && (pctx->fp[idx] != stdout)) {
                        metadata_finalize_header(pctx->fp[idx]) ;
                        // Create hash of the metadata chunk  before the file descriptor is closed. 
					    update_HMAC_SHA1_metadata(pctx->fp[idx]);

                        if ((fclose(pctx->fp[idx])) != EXIT_SUCCESS) {
                            syslog(LOG_ERR, "%s: fclose() failed to close file with errno: %d", __func__, errno);
                        }

                        n = sprintf(tmpbuf, "%s", pchan->data_loc[idx].stagingdir) ;
                        strcpy(&tmpbuf[n], pctx->chunk_fname) ;

                        memset(newfname,0,sizeof(newfname));
                        strcpy(newfname, pchan->data_loc[idx].datadir) ;
                        strcat(newfname, pchan->subpath) ;
                        n = strlen(newfname) ;
                        snprintf(&newfname[n], sizeof(newfname)-n-1, "%s.%09lu.data", pctx->chunk_fname, pctx->chunk_duration_usecs) ;
                        if(rename(tmpbuf, newfname) != 0) //capturing return here, as it may fail
                        {
                            syslog(LOG_WARNING, "%s: rename() failed with errno: %d | Line: %d.\n", __func__, errno, __LINE__);
                        }
                        /*Shared Memory monitoring the staging directory, to add new chunk to 
                        shared memory we need to do to generate file system event*/
                        memset(tb,0,sizeof(tb));
                        n = sprintf(tb, "%s", pchan->data_loc[idx].stagingdir) ;
                        snprintf(&tb[n], sizeof(tb)-n-1, "%s.%09lu.data", pctx->chunk_fname, pctx->chunk_duration_usecs) ;
                        if (generate_filesystem_event(tb) != 0) {
                            syslog(LOG_ERR, "%s: failed to generate filesystem event", __func__);
                        }
#ifdef DEBUG							
					    display_HMAC_SHA1_metadata( (const char *) newfname);
#endif							

                    }
                }
            }
            metadata_reset_record_tracker() ;
            pctx->chunk_duration_usecs = 0 ;
            
            // gmtime_r(&tsec, &tm) ;                        
            gmtime_r(&(pdesc->tod.tv_sec), &tm) ;
            
            // create a new chunk 
            sprintf(pctx->chunk_fname, "%4d%02d%02d-%02d%02d%02d", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec) ;

            if ((is_new_path_required(pctx, tm) == true)) {
                pctx->prev_tm = tm ;    
                prepare_subpath(pchan, pctx) ;
            }

            for (idx = 0; idx < MAXDATALOC; idx++) {
                if (pchan->data_loc[idx].is_used) { 
                    strcpy(&tmpbuf[0], pchan->data_loc[idx].stagingdir) ;
                    strcat(&tmpbuf[0], &pctx->chunk_fname[0] ) ;
                    
                    /* make sure the staggingdir exist */
                    if (! is_dir_exist(pchan->data_loc[idx].stagingdir)) {
                        mkdir_p(pchan->data_loc[idx].stagingdir) ;
                    }

                    new_fp = fopen(&tmpbuf[0], "w+b") ;    

                    pctx->fp[idx] = new_fp ;
                    pctx->t0 = t_start ;

                    if (pfp) {
                        pfp[idx] = new_fp ;
                        rc = 1 ;
                    }
                    if (new_fp) {
                        start.tv_sec = pdesc->ts.tv_sec ;
                        start.tv_usec = pdesc->ts.tv_nsec / 1000 ;
                        metadata_chunk_header(pctx->fp[idx],  &start) ;
                        if (verbose) {
                            fprintf(stderr, "========\n") ;
                        }
                    }   
                }
            }                
        }
        else
        {
            rc = 0 ;
        }
    }
    else {
        fprintf(stderr, "deltaT == 0, call metadata_chunk_init()\n") ;
    }

    return rc ;
}

void metadata_chunk_finalize(metadata_io_chan *pchan)
{
    int idx, n = 0;
	metadata_chunk_desc_t *pctx = &(pchan->m_chunk) ;

    char curname[MAXPATH] ;
    char newname[MAXPATH] ;
    char tb[MAXPATH] ;
    
    for (idx = 0; idx < MAXDATALOC; idx++) {
        if (pchan->data_loc[idx].is_used) { 
            if ((pctx != NULL) && (pctx->fp[idx] != NULL) && (pctx->fp[idx] != stdout)) {
		    if (pctx->chunk_duration_usecs > 0)  {
			    metadata_finalize_header(pctx->fp[idx]);
			    // Create hash of the metadata chunk  before the file descriptor is closed. 
			    update_HMAC_SHA1_metadata(pctx->fp[idx]);
			    if ((fclose(pctx->fp[idx])) != EXIT_SUCCESS) {
				    syslog(LOG_ERR, "%s: fclose() failed to close file with errno: %d", __func__, errno);
                    }

                    memset(curname,0,sizeof(curname));
                    strcpy(&curname[0], pchan->data_loc[idx].stagingdir) ;
                    strcat(&curname[0], &pctx->chunk_fname[0] ) ;

                    memset(newname,0,sizeof(newname));
                    strcpy(&newname[0], pchan->data_loc[idx].datadir) ;
                    strcat(&newname[0], pchan->subpath) ;
                    strcat(&newname[0], &pctx->chunk_fname[0] ) ;
                    n = strlen(&newname[0]) ;
                    sprintf(&newname[n], ".%09lu.data", pctx->chunk_duration_usecs) ;
		            if(rename(curname, newname) != 0)
                    {
                        syslog(LOG_WARNING, "%s: rename() failed with errno: %d | Line: %d.\n", __func__, errno, __LINE__);
                    }

                    /*Shared Memory monitoring the staging directory, to add new chunk to 
                    shared memory we need to run this to generate file system event*/
                    memset(tb,0,sizeof(tb));
                    n = sprintf(tb, "%s", pchan->data_loc[idx].stagingdir) ;
                    snprintf(&tb[n], sizeof(tb)-n-1, "%s.%09lu.data", &pctx->chunk_fname[0], pctx->chunk_duration_usecs) ;
                    if (generate_filesystem_event(tb) != 0) {
                        syslog(LOG_ERR, "%s: failed to generate filesystem event", __func__);
                    }
#ifdef DEBUG							
					display_HMAC_SHA1_metadata( (const char *) newname);
#endif							

                    pctx->fp[idx] = NULL ;     
                }
            }
        }
    }
    metadata_reset_record_tracker() ;
    pctx->chunk_duration_usecs = 0 ;
}

int metadata_chunk_init(metadata_chunk_desc_t *pctx, int chunk_size)
{
	struct timespec ts, tsr ;
	long long unsigned int  t, tr ;
	time_t tsec ;
	int rc = 0 ;
	int i ;

	if (pctx) {
	    memset(pctx, 0, sizeof(*pctx)) ;
	    
        rc = clock_gettime(CLOCK_REALTIME, &tsr) ;
        if (rc == 0) {
            if (chunk_size > 0) {
                pctx->chunk_max_duration = chunk_size ;
            }
            else {
                pctx->chunk_max_duration = DEFAULT_CHUNK_RECORDS_SIZE ;
            }

            
			for (i=0; i < MAXDATALOC ; i++) {
				if (pctx->fp[i]) {
                	if ((fclose(pctx->fp[i])) != EXIT_SUCCESS) { // out file pointer (close it if it was opened)
                        syslog(LOG_ERR, "%s: fclose() failed to close file with errno: %d", __func__, errno);
                    }
	            }
				pctx->fp[i] = NULL ;
            }

            pctx->session_just_started = 1 ; // set session_just_started flag

            tr = tsr.tv_sec ;
            tr = tr * 1000000000 + tsr.tv_nsec ;

            rc = clock_gettime(CLOCK_MONOTONIC_RAW, &ts) ;

            if (rc == 0) {
                t = ts.tv_sec ;
                t = t * 1000000000 + ts.tv_nsec ;

                pctx->deltat = (tr-t) ; // calculate pctx->deltat as a difference between CLOCK_REALTIME and CLOCK_MONOTONIC_RAW
            }
            else {
                fprintf(stderr,"clock_gettime() failed, errno=%d\n", errno) ;
                rc = -1 ;
            }
        }
        else {
            fprintf(stderr,"clock_gettime() failed, errno=%d\n", errno) ;
            rc = -1 ;
        }
	}
	else {
	    rc = -1 ;
	}
	return rc ;
}
