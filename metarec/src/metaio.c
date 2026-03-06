#include <sys/types.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/types.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <string.h>
#include <syslog.h>
#include <netdb.h>

#include <stdint.h>
#include <err.h>

#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>

#include "ptc_meta_intf.h"
#include "metadata_def.h"
#include "metadata_util.h"
#include "metaintf.h"
#include "metachunk.h"
#include "metaio.h"


extern int verbose ;

static struct subpfx_2 record_tracker = { 0, 0 } ;

static rcd_idx_t *idx_data = NULL ;

static int forward_time_change_occurred = 0 ;

static void *metadata_recording_thread(void *arg) ;

pthread_mutex_t time_change_mutex;

int metadata_recording_init(metadata_io_chan* pchan)
{
	int rc = EXIT_SUCCESS;

	pthread_t tid; 

	if (pchan)
	{
		tid = pchan->worker; 

		if (tid != 0){
			pthread_kill(tid, 0); // make sure start from clean 
		}
				
		if (pthread_create(&tid, NULL, metadata_recording_thread, pchan) == 0){
			pchan->worker = tid; 
			pchan->is_active = 1;
		}
		else{
			fprintf(stderr, "Metadata recording thread creation failed : %s\n", strerror(errno)) ;
			rc = EXIT_FAILURE;
		}		
	}
	return rc;
}


void metadata_recording_stop(metadata_io_chan* pchan)
{
	if ((pchan) && (pchan->is_active)) {
		pchan->is_active = 0 ;
		pthread_join(pchan->worker, NULL) ;
	}
}

void metadata_finalize_header(FILE *fp)
{
    struct subpfx_2 *p_record_track    = &record_tracker ;
    rcd_idx_t       *p_index_data      = idx_data ;
    size_t           numItemsWritten   = 0;
    size_t           numRecordsWritten = 0;

    if ((fp) && (p_record_track) && (p_index_data) && (p_record_track->num_records > 0) ) {
        if (fseek(fp, (long)offsetof(metachunk_file_pfx_t, subpfx_2), SEEK_SET) != 0) {
            syslog(LOG_ERR, "%s: Unable to seek.  fseek() returned: %d", __func__, errno);
        }
        else {
        	numItemsWritten = fwrite((void *)p_record_track, sizeof(struct subpfx_2), 1, fp);

        	if (numItemsWritten != 1) {
        		syslog(LOG_ERR, "%s: Unable to write to file.  fwrite() returned: %ld", __func__, numItemsWritten);
            }
            else {
            	numRecordsWritten = fwrite((void *)p_index_data, sizeof(rcd_idx_t), p_record_track->num_records, fp);

            	if (numRecordsWritten != p_record_track->num_records) {
            		syslog(LOG_ERR, "%s: Unable to write to file.  fwrite() returned: %ld", __func__, numRecordsWritten);
            	}
            }
        }
    }
}

void metadata_reset_record_tracker()
{
	struct subpfx_2 *p_record_track = &record_tracker ;

	if (p_record_track) {
		p_record_track->num_records = 0 ;
	}
}

int is_meta_record_reach_max(metadata_chunk_desc_t *pctx)
{
	struct subpfx_2 *p_record_track = &record_tracker ;

	return ((p_record_track->num_records >= p_record_track->max_records) || (p_record_track->num_records >= pctx->chunk_max_duration));
}

void update_forward_time_change_status(int status)
{
	pthread_mutex_lock(&time_change_mutex);
	forward_time_change_occurred = status;
	pthread_mutex_unlock(&time_change_mutex);
}

int has_forward_time_change_occurred()
{
	int status = 0;

	pthread_mutex_lock(&time_change_mutex);
	status = forward_time_change_occurred;
	pthread_mutex_unlock(&time_change_mutex);

	return (status);
}

/* 
 * is_drive_status_updated to check whether the drive
 * status has been updated or not; It will compare current
 * recording status to its previous known status
 * 
 * @param[in] rm - current commanded recording mode
 * 
 * @return 1 if updated, 0 otherwise
 */
static int is_drive_status_updated(int rm) 
{
	static int pre_chm_rec = -1 ; 
	static int pre_ssd_rec = -1 ; 

	int chm_rec = IS_CHM_METADATA_RECORDING_COMMANDED(rm) ;
	int ssd_rec = IS_SSD_METADATA_RECORDING_COMMANDED(rm) ;

	int is_updated = 0 ;

	/* check CHM recording status */
	if (pre_chm_rec != -1) 
	{
		if (pre_chm_rec != chm_rec) {
			syslog(LOG_NOTICE, "%s: CHM Metadata recording status changed to [%d]\n", __func__, chm_rec) ;
			is_updated = 1 ;
		}
	}
	
	/* check SSD recording status */
	if (pre_ssd_rec != -1) 
	{
		if (pre_ssd_rec != ssd_rec) {
			syslog(LOG_NOTICE, "%s: SSD Metadata recording status changed to [%d]\n", __func__, ssd_rec) ;
			is_updated = 1 ;
		}
	}

	/* update the previous status */
	pre_chm_rec = chm_rec ;
	pre_ssd_rec = ssd_rec ;

	return is_updated ;
}

/* 
 * drive_status_update_handler to handle the drive status 
 * update, which will close current recording and update the
 * drive availability for later recording to resume; SHUTDOWN
 * STATUS msg will be sent if either chm or ssd stopped recording
 * 
 * @param[in] pchan - pointer of rtpio_chan
 * @param[in] rm 	- current commanded recording mode
 * 
 * @return N/A
 */
static void drive_status_update_handler(metadata_io_chan *pchan, int rm)
{
	// close current recording chunks 
	metadata_chunk_finalize(pchan);

	// read the new recoridng mode and disable the location 
	pchan->data_loc[0].is_used = IS_CHM_METADATA_RECORDING_COMMANDED(rm) ;
	pchan->data_loc[1].is_used = IS_SSD_METADATA_RECORDING_COMMANDED(rm) ; 
}


static void done(metadata_io_chan *pchan)
{
    metadata_chunk_finalize(pchan);
}


static void write_metadata(FILE **out, metadata_io_chan *pchan, metadata_desc_t *pdesc)
{	
	if (pchan == NULL || pdesc == NULL) return ;

	int i = 0 ;
	
	for (i=0; i < pchan->n_copies; i++) {
		if ((pchan->data_loc[i].is_used) && (out[i] != NULL)) {			
			if (fwrite(pdesc->pData, pdesc->data_length, 1, out[i]) == 0) {
				perror("fwrite");
				syslog(LOG_ERR, "%s:error writing metadata data to %s\n", pchan->src_premise, pchan->data_loc[i].dispname) ;
				continue ;
			}
		}
	}	
}

static void mask_signals_in_thread(void)
{
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);

    pthread_sigmask(SIG_BLOCK, &mask, NULL);
}


static void *metadata_recording_thread(void *arg)
{
	metadata_io_chan *pchan = (metadata_io_chan *) arg ;

	static metadata_desc_t mdesc;
	metadata_desc_t *pdesc = &mdesc; 
	
	FILE *out[MAXDATALOC] = { NULL, NULL } ;
	
	unsigned int pos = 0 ;

	int i = 0, rm = 0 ;

	static unsigned cnt = 0 ;

    if (pchan == NULL) {
        //pchan->is_active = 0 ;
    	return NULL;
    }

    mask_signals_in_thread() ;

    pchan->is_active = 1 ;

    /* allocate and initialize keyframe tracking structures */
    idx_data = calloc((2048 - sizeof(struct subpfx_1) - sizeof(struct subpfx_2)) / sizeof(rcd_idx_t), sizeof(rcd_idx_t)) ;

    if (idx_data) {
    	record_tracker.max_records = (2048 - sizeof(struct subpfx_1) - sizeof(struct subpfx_2)) / sizeof(rcd_idx_t) ;
    	record_tracker.num_records = 0 ;
    }

    metadata_chunk_init(&(pchan->m_chunk), pchan->chunk_duration);

	init_metadata_recording_queue();

    while (pchan->is_active) 
    {     	
        memset(pdesc, 0, sizeof(metadata_desc_t));

		if (get_metadata_descriptor(pdesc))
    	{
			if (pdesc){				// get a record to write 

				cnt += 1;

				rm = ptci_get_rm_meta(); /* get current recording mode */
				pchan->m_chunk.drive_status_updated = is_drive_status_updated(rm);

				/* handle the status update, could be turn on/shutdown */
				if (pchan->m_chunk.drive_status_updated) {
					drive_status_update_handler(pchan, rm);
				}

				if (get_metadata_chunk_file(pchan, pdesc, &out[0])) { 	/* new chunk file created ? */
					pos = sizeof(metachunk_file_pfx_t) + sizeof(metadata_chunk_hdr_t); /* set pos to default start location */				
				}

				if ((idx_data) && ( record_tracker.num_records < record_tracker.max_records)) 
				{
					idx_data[record_tracker.num_records].byteoffset = pos ;
					idx_data[record_tracker.num_records].timestamp = pdesc->tod.tv_sec ;
					idx_data[record_tracker.num_records].recordtype = pdesc->data_type ;
					idx_data[record_tracker.num_records].recordlength = pdesc->data_length ;
					record_tracker.num_records += 1 ;
				}
					
				if (verbose) {
					fprintf(stderr, "I=%u, file_offset=%u == %08X, timestamp=%d\n",
						cnt, pos, pos, (int)(pdesc->tod.tv_sec)) ;
				}
				
											
				// write to file
				if ((out[0]!= NULL) || (out[1]!=NULL)) {
					write_metadata(out, pchan, pdesc);					
					/*
					 * we assume all copies of the chunk to be identical, so all active file pointers
					 * would 'ftell()' the same file offset. The first one is OK for us.
					 */
					for (i=0; i < MAXDATALOC; i++) {
						if (out[i] != NULL) {
							pos = ftell(out[i]) ;
							break ;
						}
					}					
				}

				if (pdesc->pData) {
				    free(pdesc->pData) ;
				}
			}
    	}
    	else {
    		usleep(300000);  // take a 300 millsecs nap
    		// does it elapse the max duration yet? a new chunk will be created until next valid record
    		metadata_chunk_desc_t *pctx = &(pchan->m_chunk) ;

			if((out[0]!= NULL) || (out[1]!=NULL)){		
				// update the duration based on last known descriptor
				long long unsigned int t1 ;
				struct timespec ts; 

				clock_gettime(CLOCK_MONOTONIC_RAW, &ts) ;
				
	            t1 = ts.tv_sec ;
	            t1 = t1 * 1000000000 + ts.tv_nsec ;

		     pctx->chunk_duration_usecs = (unsigned int) ((t1 - pctx->t0) / 1000) ;
	            if (t1 >= (pctx->t0 + (pctx->chunk_max_duration*1000000000LL))){
    				metadata_chunk_finalize(pchan); 	
    				pctx->session_just_started = 1 ;
				out[0]= NULL;
				out[1]= NULL;
	            }
        	}
    	}
    }

    done(pchan) ;

    if (idx_data) {
    	free(idx_data) ;
    	record_tracker.num_records = 0 ;
    	record_tracker.max_records = 0 ;
    }

    if (verbose) {
    	fprintf(stderr, "Exit Metadata recording thread...\n") ;
    }

    return NULL ;
}
