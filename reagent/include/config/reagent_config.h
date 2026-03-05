#ifndef _REAGENT_CONFIG_H_
#define _REAGENT_CONFIG_H_

#include "expat.h"
#include "xmlutil.h"

#include "defconfig.h"
#include "video_api.h"

#define OVCONFIG_TEMPLATE "/opt/reagent/bin/ov_camera_config -q -r %d -c %d -f %d %s"

#ifndef MAX_SER_NUM_SIZE
#define MAX_SER_NUM_SIZE 7
#endif

#ifndef MAX_CAM_NAME_SIZE
#define MAX_CAM_NAME_SIZE 12
#endif

#ifndef MAXDATALOC
#define MAXDATALOC 2				/* normally: CHM, RSSD */
#endif

#define MAX_URI 64
#define MAX_CAMS 16
#define MAX_RECORDERS 8

#define BUFF_SIZE_1KB 1024

#define REAGENT_BASE_CONFIG_FILE "/opt/reagent/config/base_config.xml"
#define REAGENT_CURRENT_CONFIG_FILE "/opt/reagent/config/current_config.xml"
#define REAGENT_APP "/opt/reagent/bin/reagent"

#define RTSPREC_FULL_PATH "/opt/reagent/bin/rtsprec"
#define RTSPREC_BASE_NAME "rtsprec"

/* Lightweight word expansion stuff ... */
#define LWE_MAXARG 32

typedef struct {
	size_t		lwe_wordc;
	char		**lwe_wordv;
	size_t		lwe_offs;
}	lwe_t;

extern int l_wordexp(const char *__words, lwe_t *__pwordexp, int __unused_flags) ;
extern void l_wordfree(lwe_t *__pwordexp) ;

#define RTSPREC_APP "/opt/reagent/bin/rtsprec"
#define METAREC_APP "/opt/reagent/bin/metarec"
#define SPACEMON_APP "/opt/reagent/bin/space_monitor"

////////////////////////////////

/////////////////////////////////////////////////////////////////
/* individual camera definition parameters */
typedef struct {
	char id[4] ;			/* Camera ID 1 .. 16 */
	char name[16] ;			/* Camera Name (as per Main configuration */
    char disp_mac[22] ;         /* MAC address string, as it per .xml file. May contain ":" or "-" */
	char model[22] ;			/* Camera Model */
    char homepage[MAX_URI] ;	/* Camera HTTP HomePage, if any */
	char stream_url[MAX_URI] ;	/* URI to use for RTSP streaming */
	char snapshot_url[MAX_URI] ;	/* URI to use for snapshots */
	char mjpeg_url[MAX_URI] ;	/* URI to use for MJPEG streaming */
	char chm_auto_rec[4] ;		/* atomatically start recordong to CHM when available ? */
	char rssd_auto_rec[4] ;		/* atomatically start recordong to removable SSD when available ? */
	char resolution[4] ;		/* 0 .. 3 */
	char compression[4] ;		/* 0 .. 3 */
	char frame_rate[4] ;		/* 0 .. 3 */
	char do_audio[4] ;			/* T if audio should be recorded, F otherwise */
	char mount_type[4];			/* mount type for 360 camera, “0�? – None, “1�? – Wall, “2�? - Ceiling */
	char accessory_pwr[4] ;		/* 0 or 1 */
	char is_essential[4];          /* 0 or 1 */
	char max_age[4];		     /* max recording duration in seconds. oldest chunks will be deleted to keep
									recording in threshold limit */
	/* the parameters below are not a part of an .xml configuration. They are set [as commanded] at runtime */
	//char is_audio_rec[4]      /* 0 = Audio On, 1 = Audio Off, 3 = Low */
	struct {
		struct cam_runtime_s {
			int is_assigned	;	/* is camera assigned == ((disp_mac[0]!='\0) && (stream_url[0]!='\0')) */
			int is_online ;		/* set by [future] camera monitor thread */
			int resolution ; 	/* 0..3 */
			int compression ; 	/* 0..3 */
			int frame_rate ;	/* 0..3 */
			int max_age;
			int is_essential;
			unsigned int recording_mode_flags ;		/* see DO_RECORD_... #defines below */
			unsigned int recording_status_flags ;	/* see IS_RECORDING_... #defines below */
			unsigned int commanded_paused ;			/* 0 => Commanded Recording 1 => Commanded Paused*/
		} runtime ;

	    pid_t worker_pid ;      /* worker process PID, if running, 0 otherwise */
		char worker_cmdline[BUFF_SIZE_1KB]; /* worker process command line string for this camera */
		/*
		 * pre-constructed (based on the command line) argc and argv
		 * used by the worker process goes here.
		 * Note: points to a dynamically allocated memorty, should be released using the l_wordfree() call
		 *
		 */
		lwe_t we ;
	    /*
	     * video chunks subdirectory -- part of video chunk path below the video_footage root,
	     * based on current <loco_id> <cam_id> <cam_name> <cam_MAC> combination
	     */
	    char vchunk_subdir[64] ;
	};
} camdef_t ;

/*
 * camera recording mode 'as commanded' flag definitions. See the 'runtime.recording_mode_flags' above
 * Note that these bits only define the corresponding 'permission to record' when recording from this camera
 * Actual recording activity is controlled by a separate API message -- CAMERA_RECORD_PAUSE
 */
#define DO_RECORD_CHM_VIDEO	0x01
#define DO_RECORD_CHM_AUDIO	0x02
#define DO_RECORD_SSD_VIDEO	0x04
#define DO_RECORD_SSD_AUDIO	0x08
#define DONT_USE_CHM		0x10	/* non-essential camera */

/* camera [current] recording status flag definitions. See the 'runtime.recording_status_flags' above */
#define IS_RECORDING_CHM_VIDEO	0x01
#define IS_RECORDING_CHM_AUDIO	0x02
#define IS_RECORDING_SSD_VIDEO	0x04
#define IS_RECORDING_SSD_AUDIO	0x08
/////////////////////////////////////////////////////////////////

/* Metadata recording flags based on drive ready status */
#define DO_RECORD_CHM_META 	0x01
#define DO_RECORD_SSD_META  0x02

typedef struct 
{
	pid_t worker_pid ; 
	char worker_cmd[512]; 
	lwe_t we;
} meta_recorder_t;

typedef struct 
{
	pid_t worker_pid ; 
	char worker_cmd[1024]; 
	lwe_t we;
} space_mon_t;

typedef struct {

	char rec_ser_num[12] ;		/* recorder serial number */
	char chunk_length[8] ;		/* max video chunk duration, in seconds */
	char staging_base[MAX_URI] ;	/* video data staging base direcotory */
	char chm_footage_base[MAX_URI] ;
	char rssd_footage_base[MAX_URI] ;
	char max_age[12];
	camdef_t cameras[MAX_CAMS] ;
	int n_cameras ;					/* number of configured cameras */
	meta_recorder_t metaworker;
	space_mon_t     spacemonitor;
    int camon_sock ;	/* socket used for camera availability monitoring */
	/* recorder-wide parameters which are set at runtime */
	struct media_runtime_s {
		int is_chm_ready ;		/* 1 - CHM is available for recording */
		int is_rssd_ready ;		/* 1 - RSSD is available for recording */
		int n_cams_recording ;	/* total number of cameras currently recording on any disk */
		char loco_id[LOCO_ID_STORAGE_SIZE] ;			/* current locomotive ID */		
	} runtime ;
} media_info_t;

/* up to 16 recorders may be configured */
typedef struct {
	cfg_file_t cfg ;		/* conf. file descriptor */
	media_info_t media ;	/* media information */
} media_conf_t ;

extern int raconf_lock() ;
extern int raconf_unlock() ;
extern int raconf_init(int argc, char **argv) ;
extern int raconf_parse(int argc, char **argv);
extern void raconf_show() ;
extern void raconf_show_single(int i) ;
extern media_info_t *raconf_get_media_info(int irec) ;
extern camdef_t * raconf_find_camera(media_info_t *p_minfo, char * p_name) ;
extern int raconf_prepare_for_recording(media_info_t *p_media, camdef_t *p_camdef) ;
extern void raconf_prepare_for_metarecording(media_info_t *p_media) ;
extern int str2bool(char *p);
#endif
