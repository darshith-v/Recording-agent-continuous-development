#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <wordexp.h>
#include <sys/types.h>

#ifdef QNX_BUILD
#include <sys/wait.h>
#else
#include <wait.h>
#endif
#include <signal.h>

#include <pthread.h>
#include <syslog.h>

#include "reagent_config.h"
#include "user_config.h"
#include "video_api.h"

#include "ovconfig.h"
#include "camon.h"

/* media configuration for all supported recorders */
static media_conf_t media_conf[MAX_RECORDERS];
static int n_recorders = 0 ;
static pthread_mutex_t conf_lock = PTHREAD_MUTEX_INITIALIZER ;

/* template/temporary media configuration goes gere */
static media_conf_t tmp_conf ;

/* describe .xml configuration file tag structure, initialize pointers to the elements of tmp_conf above */
static cfg_param_t cfg_tag_tpl[] = {
		/* per recorder parameters */
		{1, 0, "serial_number", &tmp_conf.media.rec_ser_num[0], MAX_SER_NUM_SIZE+1, NULL, 0 },
		{1, 0, "loco_id", &tmp_conf.media.runtime.loco_id[0], MAX_LOCO_ID_SIZE+1, NULL, 0 },
		{1, 0, "video_storage", NULL, 0, NULL, 0 },
		{1, 0, "camera", NULL, sizeof(camdef_t), NULL, 0 },
		/*video storage related, per recorder*/
		{2, 0, "staging_base_dir", &tmp_conf.media.staging_base[0], MAX_URI, NULL, 0 },
		{2, 0, "video_chunk_duration", &tmp_conf.media.chunk_length[0], 6, NULL, 0 },	/* 6 figures max would be enough */
		{2, 0, "chm_video_base_dir", &tmp_conf.media.chm_footage_base[0], MAX_URI, NULL, 0 },
		{2, 0, "rssd_video_base_dir", &tmp_conf.media.rssd_footage_base[0], MAX_URI, NULL, 0 },
		{2, 0, "max_age", &tmp_conf.media.max_age[0], 12, NULL, 0 },
		/*  camera related, per recorder per camera */
		/***{2, 1, "camera_id", &tmp_conf.media.cameras[0].id[0], 3, NULL, 0 }, ***/				/* 3 figures max would be enough */
		{2, 1, "name", &tmp_conf.media.cameras[0].name[0], CAMERA_NAME_LENGTH+1, NULL, 0 },
		{2, 1, "serial", &tmp_conf.media.cameras[0].disp_mac[0], 18+1, NULL, 0 },
		{2, 1, "model", &tmp_conf.media.cameras[0].model[0], 22-1, NULL, 0 },
		{2, 1, "home_page", &tmp_conf.media.cameras[0].homepage[0], MAX_URI, NULL, 0 },
		{2, 1, "stream_url", &tmp_conf.media.cameras[0].stream_url[0], MAX_URI, NULL, 0 },
		{2, 1, "mjpeg_url", &tmp_conf.media.cameras[0].mjpeg_url[0], MAX_URI, NULL, 0 },
		{2, 1, "snapshot_url", &tmp_conf.media.cameras[0].snapshot_url[0], MAX_URI, NULL, 0 },
		{2, 1, "auto_record_to_chm", &tmp_conf.media.cameras[0].chm_auto_rec[0], 4, NULL, 0 },
		{2, 1, "auto_record_to_rssd", &tmp_conf.media.cameras[0].rssd_auto_rec[0], 4, NULL, 0 },
		{2, 1, "camera_essential", &tmp_conf.media.cameras[0].is_essential[0], 4, NULL, 0 },
		{2, 1, "camera_max_recording_age", &tmp_conf.media.cameras[0].max_age[0], 4, NULL, 0 },
		{2, 1, "resolution", &tmp_conf.media.cameras[0].resolution[0], 4, NULL, 0 },
		{2, 1, "compression", &tmp_conf.media.cameras[0].compression[0], 4, NULL, 0 },
		{2, 1, "frame_rate", &tmp_conf.media.cameras[0].frame_rate[0], 4, NULL, 0 },
		{2, 1, "record_audio", &tmp_conf.media.cameras[0].do_audio[0], 4, NULL, 0 },
		{2, 1, "mount_type", &tmp_conf.media.cameras[0].mount_type[0], 4, NULL, 0 },
		{2, 1, "accessory_power", &tmp_conf.media.cameras[0].accessory_pwr[0], 4, NULL, 0 },
};
static int n_cfg_tags = sizeof(cfg_tag_tpl)/sizeof(cfg_param_t) ;

/*
 * Example:
    rtsprec -l BNSF_12345 -c CAB_FORWARD -m ACC8E982FEB rtsp://10.10.9.102:554/onvif-media/media.amp
*/

/*
 * command template for launching rtsprec/start recording from a camera
 * recording to both CHM and RSSD is always initiated.
 * loco_id, camera_name, camera_serial, chunk duration, CHM data_location, SSD data_location and RTSP URL strings should be substituted
 * in this template in order to make a command
 */
static const char *cmd_tpl =
	"%s -l %s -c %s -m %s -d %s -r %02X -f %d -1 %s -2 %s %s" ;

// command template to start the metarec, 
// locomotive id, chunk size, recording location index [1, 2], recording location
static const char *cmd_tpl_metarec =
    "%s -l %s -d %s -r %02X -1 %s -2 %s" ;
// eg. metarec -l BNSF_1234 -d 120 -r 0x03 -1 /mnt/chmSSD/video/VideoFootages -2 /mnt/removableSSD/video/VideoFootages

static const char *cmd_tpl_spacemon =
    "%s -l %d %s %s" ;

/* Lightweight word expansion stuff ... */
int l_wordexp(const char *__words, lwe_t *__pwordexp, int __unused_flags)
{
	int rc = -1 ;
	int cmdlen ;

	if ((__words != NULL) && ((cmdlen=strlen(__words)) != 0)) {
		int memsize = (LWE_MAXARG+1) * sizeof(char *) + (cmdlen+1) ;
		char *pmem, *pwords ;

		if ((pmem=malloc(memsize)) != NULL) {
			char *rest = NULL;
			char *token;

			__pwordexp->lwe_wordv = (char **)pmem ;
			pwords = pmem + (LWE_MAXARG+1) * sizeof(char *) ;
			memset(pmem, 0, memsize) ;
			memcpy(pwords, __words, cmdlen) ;

			__pwordexp->lwe_wordc = 0 ;
			for (token = strtok_r(pwords, " ", &rest); token != NULL; token = strtok_r(NULL, " ", &rest)) {
				__pwordexp->lwe_wordv[__pwordexp->lwe_wordc] = token ;
				__pwordexp->lwe_wordc += 1 ;
				if (__pwordexp->lwe_wordc >= LWE_MAXARG) {
					break ;
				}
			}
			rc = 0 ;
		}
	}
    return rc ;
}

void l_wordfree(lwe_t *__pwordexp)
{
	char *p ;
	if ((__pwordexp != NULL) && ((p = (char *)__pwordexp->lwe_wordv) != NULL)) {
		free(p) ;
	}
}
/****************/

int raconf_lock()
{
	return pthread_mutex_lock(&conf_lock) ;
}

int raconf_unlock()
{
	return pthread_mutex_unlock(&conf_lock) ;
}

/**
 * Helper function to convert string to boolean
 *
 * @param[in] *p Pointer to string
 *
 * @return     0 if String interpreted as False
 * @return     1 if String interpreted as True
 */
int str2bool(char *p)
{
    int bval = 0;

    if (p)
    {
        if ((*p == '1') || (*p == 'T') || (*p == 't') || (*p == 'Y') || (*p == 'y'))
        {
            bval = 1;
        }
    }
    return (bval);
} //str2bool


/*
 * function to prepare a clean command line for starting metarec
 * flags are being set based on drive ready status 
 */
void raconf_prepare_for_metarecording(media_info_t *p_media)
{
	unsigned int metarec_flags = 0 ; 
	meta_recorder_t *pmeta_rec ;
	int status = -1;

	if (p_media) {
		pmeta_rec = &(p_media->metaworker) ;

		if (pmeta_rec->worker_pid) {
			kill(pmeta_rec->worker_pid, SIGHUP) ;
			waitpid(pmeta_rec->worker_pid, &status, 0) ;
			pmeta_rec->worker_pid = 0;
			syslog(LOG_INFO, "%s: metarec worker pid %d killed ", __func__, pmeta_rec->worker_pid);			
		}

		if (pmeta_rec->we.lwe_wordv) {
			l_wordfree(&(pmeta_rec->we)) ;
			memset(&(pmeta_rec->we), 0, sizeof(pmeta_rec->we)) ;
		}

		if (p_media->runtime.is_chm_ready) {
			metarec_flags |= DO_RECORD_CHM_META; 
		}

		if (p_media->runtime.is_rssd_ready) {
			metarec_flags |= DO_RECORD_SSD_META; 
		}

		memset(&(pmeta_rec->worker_cmd[0]), 0, sizeof(pmeta_rec->worker_cmd));
	
		snprintf(pmeta_rec->worker_cmd, sizeof(pmeta_rec->worker_cmd)-1,
			 cmd_tpl_metarec,
			 METAREC_APP,
			 p_media->runtime.loco_id, p_media->chunk_length, metarec_flags, 
			 p_media->chm_footage_base, p_media->rssd_footage_base) ;

		l_wordexp(pmeta_rec->worker_cmd, &(pmeta_rec->we), 0 /*WRDE_SHOWERR|WRDE_NOCMD*/) ;
	}
}

/*
 * called every time when camera recording is being started.
 * prepares a 'rtsprec' command line and attempts to configure a camera
 * returns 0 on success, nonzero if the camera id offline or cannot be configured
 */
int raconf_prepare_for_recording(media_info_t *p_media, camdef_t *p_camdef)
{
	char tmpbuf[256] = {0};
	char canonical_mac[16] = {0};
	int k = 0;
	int rc = 0;
	char tmp_worker_cmdline[BUFF_SIZE_1KB] = {0};

    /* convert MAC addres to a canonical form, pad with zeros on the right, if needed*/
	memset(&canonical_mac[0], 0, sizeof(canonical_mac)) ;
    {
        char *psrc = &(p_camdef->disp_mac[0]) ;
        char *pdst = &(canonical_mac[0]) ;

        for (k=0; k < 12 ; ++psrc) {
            if (((*psrc >= '0') && (*psrc <= '9')) || ((*psrc >= 'A') && (*psrc <= 'F')) || ((*psrc >= 'a') && (*psrc <= 'f')) ) {
                *pdst++ = *psrc ;
                ++k ;
            }
            else {
                if (*psrc == '\0') {
                    break ;
                }
            }
        }

        k = strlen(&(canonical_mac[0])) ;
        if (k < 12) {
            memset(&(canonical_mac[k]), '0', 12-k) ;
        }
    }

    //Ensuring null-termination of buffers.
	//Preventing out-of-bound read/access.
	memcpy(&tmpbuf[0], &p_camdef->stream_url[0], sizeof(p_camdef->stream_url));
	snprintf(tmp_worker_cmdline, BUFF_SIZE_1KB,
			 cmd_tpl,
			 RTSPREC_APP,
			 p_media->runtime.loco_id, p_camdef->name, canonical_mac, p_media->chunk_length,
			 p_camdef->runtime.recording_mode_flags, 0,
			 p_media->chm_footage_base, p_media->rssd_footage_base,
			 &tmpbuf[0]);

	memcpy(p_camdef->worker_cmdline, tmp_worker_cmdline, BUFF_SIZE_1KB);

	if (p_camdef->we.lwe_wordv) {
		l_wordfree(&(p_camdef->we)) ;
		memset(&(p_camdef->we), 0, sizeof(p_camdef->we)) ;
	}
	l_wordexp(p_camdef->worker_cmdline, &(p_camdef->we), 0 /*WRDE_SHOWERR|WRDE_NOCMD*/) ;

	/* attempt to configure the camera, if online */
	if (p_camdef->runtime.is_online) {
		rc = ovc_set_date_and_time_direct(p_camdef->homepage, NULL, eTimeSystemtime) ;
		if (rc == EXIT_SUCCESS) {
			syslog(LOG_INFO, "Camera '%s': successfully synchronized system time\n", p_camdef->name) ;
		}
		else {
			syslog(LOG_ERR, "Camera '%s': failed to synchrozize system time\n", p_camdef->name) ;
		}

		rc = ovc_set_rcf_direct(p_camdef->homepage, p_camdef->runtime.resolution, p_camdef->runtime.compression, p_camdef->runtime.frame_rate);
		if (rc == EXIT_SUCCESS) {
			syslog(LOG_INFO, "successfully configured camera '%s' at %s\n", p_camdef->name, p_camdef->homepage) ;
		}
		else {
			syslog(LOG_ERR, "failed to configure camera '%s' at %s\n", p_camdef->name, p_camdef->homepage) ;
		}
	}
	else {
		rc = 1 ;
	}

	return rc ;
}

int raconf_parse(int argc, char **argv)
{
	int i,j,k;
	cfg_param_t cfg_tags[n_cfg_tags];
	camdef_t *p_camdef ;

	if ((argc > 1) && (strcmp(argv[1],"-") != 0)) {
		char tmpbuf[256] ;

		n_recorders = 0 ;
		memset(&media_conf, 0, sizeof(media_conf)) ;

		for (i = 1; (i < argc) && (i <= MAX_RECORDERS); i++) {
			media_conf[n_recorders].cfg.filename = argv[i];
			media_conf[n_recorders].cfg.params = &cfg_tags[0];
			media_conf[n_recorders].cfg.nparams = sizeof(cfg_tag_tpl) / sizeof(cfg_tag_tpl[0]);

			memset(&tmp_conf, 0, sizeof(tmp_conf));
			memcpy(&cfg_tags, &cfg_tag_tpl, sizeof(cfg_tag_tpl));

			tmp_conf.cfg.filename = argv[i];
			tmp_conf.cfg.params = &cfg_tags[0];
			tmp_conf.cfg.nparams = sizeof(cfg_tag_tpl) / sizeof(cfg_tag_tpl[0]);

			if (get_cfg_params(&tmp_conf.cfg, 1) > 0) {
				memcpy(&media_conf[n_recorders].media, &tmp_conf.media, sizeof(media_info_t));
				++n_recorders;
			}
		}

		/* try base configuration if no recorder definitions found */
		if (n_recorders == 0) {
			media_conf[n_recorders].cfg.filename = REAGENT_BASE_CONFIG_FILE ;
			media_conf[n_recorders].cfg.params = &cfg_tags[0];
			media_conf[n_recorders].cfg.nparams = sizeof(cfg_tag_tpl) / sizeof(cfg_tag_tpl[0]);

			memset(&tmp_conf, 0, sizeof(tmp_conf));
			memcpy(&cfg_tags, &cfg_tag_tpl, sizeof(cfg_tag_tpl));

			tmp_conf.cfg.filename = argv[i];
			tmp_conf.cfg.params = &cfg_tags[0];
			tmp_conf.cfg.nparams = sizeof(cfg_tag_tpl) / sizeof(cfg_tag_tpl[0]);

			if (get_cfg_params(&tmp_conf.cfg, 1) > 0) {
				memcpy(&media_conf[n_recorders].media, &tmp_conf.media, sizeof(media_info_t));
				++n_recorders;
			}
		}
		for (i=0; i < n_recorders; i++) {

			for (j=0; j < MAX_CAMS; j++) {

				if (media_conf[i].media.cameras[j].name[0] == '\0') {
					break ;
				}
				media_conf[i].media.n_cameras += 1 ;
				p_camdef = &media_conf[i].media.cameras[j] ;

                /* perform initial default setting for those elements that not controlled by .XML configuration */
				p_camdef->runtime.resolution = atoi(p_camdef->resolution) ;
				p_camdef->runtime.compression = atoi(p_camdef->compression) ;
				p_camdef->runtime.frame_rate = atoi(p_camdef->frame_rate) ;
				p_camdef->runtime.is_essential = atoi(p_camdef->is_essential);

				p_camdef->runtime.is_assigned = ((p_camdef->disp_mac[0] != '\0') && (p_camdef->stream_url[0] != '\0')) ;
				p_camdef->runtime.is_online = 0;//p_camdef->runtime.is_assigned ;

				p_camdef->runtime.recording_mode_flags = 0 ;
				p_camdef->runtime.recording_status_flags = 0 ;
				p_camdef->runtime.commanded_paused = 0;		// cameras are assumed to be "recording" unless a pause message comes across

                /* just in case, pad loco_id and camera name with '_' on the right, if needed */
                k = strlen(&(media_conf[i].media.runtime.loco_id[0])) ;
                if (k < MAX_LOCO_ID_SIZE) {
                    memset(&(media_conf[i].media.runtime.loco_id[k]), '_', MAX_LOCO_ID_SIZE-k) ;
                }
                k = strlen(&(p_camdef->name[0])) ;
                if (k < MAX_CAM_NAME_SIZE) {
                    memset(&(p_camdef->name[k]), '_', MAX_CAM_NAME_SIZE-k) ;
                }
			}
		}
	}
	return n_recorders;
}

int raconf_init(int argc, char **argv)
{
	int rc = 0;
	int i = 0, j = 0, k = 0, m = 0;
	camdef_t *p_camdef;
	meta_recorder_t *pmeta_rec;
	space_mon_t *p_space_mon;
	cfg_param_t cfg_tags[n_cfg_tags];
	int max_age = 0;
	ready_status_t *rs = NULL;

	if ((argc > 1) && (strcmp(argv[1], "-") != 0)) {
		char tmpbuf[256] = {0};
		char *pNIC = getenv("ONVIF_PROBE_NIC") ;

		if ((pNIC == NULL) || (pNIC[0] == '\0')) {
			pNIC = ONVIF_PROBE_NIC ;
		}

		n_recorders = 0 ;
		memset(&media_conf, 0, sizeof(media_conf)) ;

		/* set RTSPREC env. variable, if it is not currently defined */
	    setenv("RTSPREC", "/opt/reagent/bin/rtsprec", 0) ;

	    /* set METAREC env. variable, if it is not currently defined */
	    setenv("METAREC", "/opt/reagent/bin/metarec", 0) ;

	    /* set SPACEMON env. variable, if it is not currently defined */
	    setenv("SPACEMON", "/opt/reagent/bin/space_monitor", 0) ;

		for (i = 1; (argv[i] != NULL) && (i <= MAX_RECORDERS); i++) {
			media_conf[n_recorders].cfg.filename = argv[i];
			media_conf[n_recorders].cfg.params = &cfg_tags[0];
			media_conf[n_recorders].cfg.nparams = sizeof(cfg_tag_tpl) / sizeof(cfg_tag_tpl[0]);

			memset(&tmp_conf, 0, sizeof(tmp_conf));
			memcpy(&cfg_tags, &cfg_tag_tpl, sizeof(cfg_tag_tpl));

			tmp_conf.cfg.filename = argv[i];
			tmp_conf.cfg.params = &cfg_tags[0];
			tmp_conf.cfg.nparams = sizeof(cfg_tag_tpl) / sizeof(cfg_tag_tpl[0]);

			if (get_cfg_params(&tmp_conf.cfg, 1) > 0) {
				memcpy(&media_conf[n_recorders].media, &tmp_conf.media, sizeof(media_info_t));
				++n_recorders;
			}
		}

		/* try base configuration if no recorder definitions found */
		if (n_recorders == 0) {
			media_conf[n_recorders].cfg.filename = REAGENT_BASE_CONFIG_FILE ;
			media_conf[n_recorders].cfg.params = &cfg_tags[0];
			media_conf[n_recorders].cfg.nparams = sizeof(cfg_tag_tpl) / sizeof(cfg_tag_tpl[0]);

			memset(&tmp_conf, 0, sizeof(tmp_conf));
			memcpy(&cfg_tags, &cfg_tag_tpl, sizeof(cfg_tag_tpl));

			tmp_conf.cfg.filename = argv[i];
			tmp_conf.cfg.params = &cfg_tags[0];
			tmp_conf.cfg.nparams = sizeof(cfg_tag_tpl) / sizeof(cfg_tag_tpl[0]);

			if (get_cfg_params(&tmp_conf.cfg, 1) > 0) {
				memcpy(&media_conf[n_recorders].media, &tmp_conf.media, sizeof(media_info_t));
				++n_recorders;
			}
		}

		for (i=0; i < n_recorders; i++) {
			int uc_ready = uc_is_ready() ;


			rs = uc_get_ready_status();
			media_conf[i].media.runtime.is_chm_ready = (rs->chm_ready) ? 1 : 0 ;
			media_conf[i].media.runtime.is_rssd_ready = (rs->removable_ssd_ready) ? 1 : 0 ;


			if (media_conf[i].media.camon_sock > 0) {		/* technically this is wrong, it _can_ be 0 */
				camon_deinit(media_conf[i].media.camon_sock) ;
			}
			media_conf[i].media.camon_sock = camon_init(pNIC) ;


			for (j=0; j < MAX_CAMS; j++) {

				if (media_conf[i].media.cameras[j].name[0] == '\0') {
					break ;
				}
				media_conf[i].media.n_cameras += 1 ;
				p_camdef = &media_conf[i].media.cameras[j] ;

                /* perform initial default setting for those elements that not controlled by .XML configuration */
				p_camdef->runtime.resolution = atoi(p_camdef->resolution) ;
				p_camdef->runtime.compression = atoi(p_camdef->compression) ;
				p_camdef->runtime.frame_rate = atoi(p_camdef->frame_rate) ;
				p_camdef->runtime.is_essential = atoi(p_camdef->is_essential);

				p_camdef->runtime.is_assigned = ((p_camdef->disp_mac[0] != '\0') && (p_camdef->stream_url[0] != '\0')) ;
				p_camdef->runtime.is_online = 0;//p_camdef->runtime.is_assigned ;

				p_camdef->runtime.recording_mode_flags = 0 ;
				p_camdef->runtime.recording_status_flags = 0 ;

				/* see if there are remnants from previous configuration init. Clean-up, if so */
				if (p_camdef->worker_pid) {
					syslog(LOG_INFO, "%s,%d: Kill camera recording process from previous configuration pid %d", __func__, __LINE__, p_camdef->worker_pid);
					kill(p_camdef->worker_pid, SIGHUP) ;
				}

				if (p_camdef->we.lwe_wordv) {
					l_wordfree(&(p_camdef->we)) ;
					memset(&(p_camdef->we), 0, sizeof(p_camdef->we)) ;
				}


                /* just in case, pad loco_id and camera name with '_' on the right, if needed */
                k = strlen(&(media_conf[i].media.runtime.loco_id[0])) ;
                if (k < MAX_LOCO_ID_SIZE) {
                    memset(&(media_conf[i].media.runtime.loco_id[k]), '_', MAX_LOCO_ID_SIZE-k) ;
                }
                k = strlen(&(p_camdef->name[0])) ;
                if (k < MAX_CAM_NAME_SIZE) {
                    memset(&(p_camdef->name[k]), '_', MAX_CAM_NAME_SIZE-k) ;
                }
			}
			
			//init space monitor
			{
				p_space_mon = &(media_conf[i].media.spacemonitor) ;
				if (p_space_mon->worker_pid) {
					syslog(LOG_INFO, "%s,%d: Kill space monitor pid %d", __func__, __LINE__, p_space_mon->worker_pid);					
				    kill(p_space_mon->worker_pid, SIGQUIT) ;
				}

				if (p_space_mon->we.lwe_wordv) {
				    l_wordfree(&(p_space_mon->we)) ;
				    memset(&(p_space_mon->we), 0, sizeof(p_space_mon->we)) ;
				}

				memset(&(p_space_mon->worker_cmd[0]), 0, sizeof(p_space_mon->worker_cmd));
				/* 
				  space monitor 
				  1- space_base: it is default mode.In this mode, monitor starts removing oldest
				     file(s) when free storage goes below 20%.
				  2- time_base: it optional mode. In this mode parameter max_age is taken from current_config.
				     In this mode monitor will start removing file(s) older than given threshold. */
				if (media_conf[i].media.max_age[0] != NULL) //beacuse it's an array, not a buffer
					max_age = atoi(media_conf[i].media.max_age);

				snprintf(p_space_mon->worker_cmd, sizeof(p_space_mon->worker_cmd)-1,
				 		 cmd_tpl_spacemon,
						 SPACEMON_APP,
				   		 max_age,  //hour base value  
				   		 media_conf[i].media.chm_footage_base,
				   		 media_conf[i].media.rssd_footage_base) ;
				l_wordexp(p_space_mon->worker_cmd, &(p_space_mon->we), 0 /*WRDE_SHOWERR|WRDE_NOCMD*/) ;
			}
		}
	}

	return n_recorders ;
}

/* shows configuration of a single recorder */
void raconf_show_single(int i)
{
	int j, k ;
	camdef_t *p_camdef ;

	if ((i>=0) && (i<n_recorders)) {

		printf("LOCO_ID = %s\n", media_conf[i].media.runtime.loco_id);
		printf("Recorder_SerialNo = %s\n", media_conf[i].media.rec_ser_num);

		printf("    %s = %s\n", "staging_base_dir", media_conf[i].media.staging_base);
		printf("    %s = %s\n", "video_chunk_duration", media_conf[i].media.chunk_length);
		printf("    %s = %s\n", "chm_video_base", media_conf[i].media.chm_footage_base);
		printf("    %s = %s\n", "rssd_video_base", media_conf[i].media.rssd_footage_base);
		printf("    %s = %d\n", "CHM ready", media_conf[i].media.runtime.is_chm_ready);
		printf("    %s = %d\n", "RSSD ready", media_conf[i].media.runtime.is_rssd_ready);

		for (j = 0; j < MAX_CAMS; j++) {
			p_camdef = &media_conf[i].media.cameras[j] ;

			if (p_camdef->name[0] == '\0') {
				break;
			}
			{
				char tmp[16] ;
				char *p_ ;

				memset(tmp, 0, sizeof(tmp)) ;
				strncpy(tmp, p_camdef->name, CAMERA_NAME_LENGTH ) ;
				if ((p_ = strrchr(tmp, '_')) != NULL) {
					while (p_ > tmp) {
						if (*(p_ -1) != '_') {
							*p_ = '\0' ;
							break ;
						}
						--p_ ;
					}
				}

				printf("  Camera #%d: %s\n", j+1, tmp);
			}

			for (k = 0; k < n_cfg_tags; k++) {
				if ((cfg_tag_tpl[k].is_per_camera != 0) && (cfg_tag_tpl[k].tag.value != NULL)
								/***&& (strcmp(cfg_tag_tpl[k].tag.name, "camera_id") != 0) ***/
								&& (strcmp(cfg_tag_tpl[k].tag.name, "name") != 0)) {
					int cam_offset;

					cam_offset = cfg_tag_tpl[k].tag.value - (char *) &tmp_conf.media.cameras[0];
					cam_offset += (j * sizeof(camdef_t));
					printf("    %s = %s\n", cfg_tag_tpl[k].tag.name, ((char *) &media_conf[i].media.cameras[0]) + cam_offset);
				}
			}

			printf("       %s = %d,%d,%d\n", "R,C,F", p_camdef->runtime.resolution, p_camdef->runtime.compression, p_camdef->runtime.frame_rate) ;
			printf("       %s = %02X\n", "is_assigned", p_camdef->runtime.is_assigned) ;
			printf("       %s = %02X\n", "is_online", p_camdef->runtime.is_online) ;
			printf("       %s = %02X\n", "recording_mode", p_camdef->runtime.recording_mode_flags) ;
			printf("       %s = %02X\n", "recording_status", p_camdef->runtime.recording_status_flags) ;

#if 0
			printf("======== %d exec() parameters ==========\n", p_camdef->we.we_wordc) ;
		    for (k = 0; k < p_camdef->we.we_wordc; k++) {
		        printf("   %d: %s\n", k, p_camdef->we.we_wordv[k]);
		    }
		    printf("========================================\n") ;
#endif
		}
	}
}

void raconf_show()
{
	int i, j, k;

	printf("%d recorder(s)\n", n_recorders);

	for (i = 0; i < n_recorders; i++) {
		raconf_show_single(i) ;
	}
}

/*
 * Returns: pointer to a media_info structure for specified recorder, NULL otherwise
 */
media_info_t *raconf_get_media_info(int irec)
{
    media_info_t *p_minfo = NULL ;

	media_info_t *p = NULL ;
	if ((irec>=0) && (irec<n_recorders)) {
	    p_minfo = &media_conf[irec].media ;
	}

	return p_minfo ;
}

/*
 * scans the array of configured cameras,
 * looks for a camera name starting with a subdsring pointed by p_name (case insensitive)
 * if found, returns a pointer to the corresponding camdef_t structure, NULL otherwise
 */
camdef_t * raconf_find_camera(media_info_t *p_minfo, char * p_name)
{
    camdef_t * p_camdef = NULL ;
    int i, n ;

    n = 0 ;
    if ((p_name!= NULL) && (p_minfo != NULL)) {
        n = strlen(p_name) ;
        if (n > 0) {
            for (i=0, p_camdef=&(p_minfo->cameras[0]); i < p_minfo->n_cameras; ++p_camdef, ++i) {
            	if (strncasecmp(p_name, p_camdef->name, n) == 0) {
                    break ;
                }
            }
            if (i >= p_minfo->n_cameras) {
                p_camdef = NULL ;
            }
        }
        else {
            printf("Empty cam name\n") ;
        }
    }
    else {
        printf("null p_name or p_minfo\n") ;
    }

    return p_camdef ;
}


