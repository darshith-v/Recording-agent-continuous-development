#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <wordexp.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <syslog.h>

#include "ptc_config.h"

#include "ptc_api.h"
#include "crc32_table.h"
#include "exit_codes.h"
#include "reagent_config.h"
#include "video_api.h"
#include "ptc_intf.h"
#include "user_config.h"
#include "video_api.h"

#include "ptc_api_globals.h"

/*
 * subroutines and data structures in this file are responsible for the 'USER' section of configuration information.
 * This info should be obtained from the main LDRS-V application on every boot using the 'video_API' messaging.
 * Having the User config section is mandatory, we cannot start normal RA operation until the following valid videoAPI/PTC messages are received:
 *
 *    INIT_CAMERA_LIST, TIME_INFO, LOCOMOTIVE_ID
 *
 * Note that there are other, optional user configuration data structures (e.g. DIGITAL_INPUTS) obtained from the main LDRS-V Application.
 */

static volatile int uc_state = 0 ;
static volatile int keep_going = 0 ;

static camera_list_element_t uc_camera_list[NUM_CAMERAS] ;
static time_info_t uc_time_info ;
static locomotive_id_t uc_loco_id ;
static ready_status_t uc_ready_status;

static void uc_msgcb_init_camera_list(void *p_user, const ptc_msg_info_t *p_info);
static void uc_msgcb_time_info(void *p_user, const ptc_msg_info_t *p_info);
static void uc_msgcb_locomotive_id(void *p_user, const ptc_msg_info_t *p_info);
static void uc_msgcb_clear_uc(void *p_user, const ptc_msg_info_t *p_info);
static void uc_msgcb_hb_request(void *p_user, const ptc_msg_info_t *p_info);
static void uc_msgcb_ready_status(void *p_user, const ptc_msg_info_t *p_info);

/*
 * only limited number of Video API messages are accepted while collecting the User Config
 */
static const ptc_msg_callback_entry_t uc_msg_cb_list[] =
{
	{ INIT_CAMERA_LIST	, uc_msgcb_init_camera_list},
	{ TIME_INFO			, uc_msgcb_time_info},
	{ LOCOMOTIVE_ID		, uc_msgcb_locomotive_id},
	{ CLEAR_USER_CONFIG	, uc_msgcb_clear_uc},
	{ HEARTBEAT_REQUEST	, uc_msgcb_hb_request},
	{ READY_STATUS      , uc_msgcb_ready_status},
};

/////////////////////////////////////////
#if 0
static const char *msg_names[] =
{
	"TIME_INFO",
	"LOCOMOTIVE_ID",
	"VIDEO_EVENT_INFO",
	"GPS_DATA",
	"CAMERA_NAME",
	"CAMERA_SETTINGS",
	"CAMERA_RESOLUTION",
	"CAMERA_COMPRESSION",
	"CAMERA_FRAME_RATE",
	"CAMERA_AUDIO",
	"CAMERA_RECORD_PAUSE",
	"READY_STATUS",
	"RECORDING_STATUS",
	"VIDEO_APPLICATION_STATUS",
	"VIDEO_APPLICATION_ERROR",
	"DOWNLOAD",
	"DOWNLOAD_ACK",
	"DOWNLOAD_STATUS",
	"SHUTDOWN",
	"SHUTDOWN_ACK",
	"HEARTBEAT_REQUEST",
	"HEARTBEAT_RESPONSE",
	"ACCESSORY_POWER",
	"REQUEST_CAMERA_STATUS",
	"CAMERA_STATUS",
	"CAMERA_SNAPSHOT",
	"CAMERA_PROFILE",
	"COMMAND_ACK",
	"INIT_CAMERA_LIST",
	"CLEAR_USER_CONFIG",
	"DIGITAL_INPUTS"
};
#endif

#define show_it(x)

/////////////////////////////////////////

/**
 * Process Ready Status Message from video_rec
 * to determine where video can be recorded to
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void uc_msgcb_ready_status(void *p_user, const ptc_msg_info_t *p_info)
{
    if ((p_info == NULL) || (p_info->p_data == NULL))
    {
        syslog(LOG_ERR, "%s: Invalid parameters", __func__);
        return;
    }

    ready_status_t *p_data = (ready_status_t *) p_info->p_data;

    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_ready_status_t);

    /* Add message-specific processing... */
    if (p_data->header.version != READY_STATUS_MSG_VERSION)
    {
        LOGF(LERR, "Unexpected ready_status_t version %d: should be %d\n", p_data->header.version, READY_STATUS_MSG_VERSION);
    }
    else
    {
        syslog(LOG_NOTICE, "%s: Ready Status message received: CHM %d, SSD %d", __func__, p_data->chm_ready, p_data->removable_ssd_ready);
		uc_ready_status.chm_ready = p_data->chm_ready;
		uc_ready_status.removable_ssd_ready = p_data->removable_ssd_ready;
	}
} //msgcb_ready_status



static void uc_msgcb_init_camera_list(void *p_user, const ptc_msg_info_t *p_info)
{
	init_camera_list_t  *p_data = (init_camera_list_t *) p_info->p_data;
	int i = 0;
	LOGF(LSYS, "uc: va_init_camera_list_t: %s\n", (uc_state & UC_HAS_CAMERA_LIST) ? "ignored" : "accepted");

	if (!(uc_state & UC_HAS_CAMERA_LIST)) {
		/* Verify message contents */
		PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_init_camera_list_t);

		/* Add message-specific processing... */
		if (p_data->header.version == INIT_CAMERA_LIST_MSG_VERSION)
		{
			show_it(p_info);
			for(i=0; i < NUM_CAMERAS && p_data->camera_list[i].camera[0] != '\0'; i++)
			{
				syslog(LOG_NOTICE,"%d] %s: Resolution %d | Compression %d | FPS %d | audio %d | essential %d | age %d ",i,
				       p_data->camera_list[i].camera,p_data->camera_list[i].resolution,p_data->camera_list[i].compression,
				       p_data->camera_list[i].frame_rate,p_data->camera_list[i].audio_enable,p_data->camera_list[i].is_essential,
				       p_data->camera_list[i].camera_max_recording_hours);
			}
			memcpy(&uc_camera_list, &(p_data->camera_list), sizeof(uc_camera_list)) ;
			uc_state |= UC_HAS_CAMERA_LIST ;
		}
		else {
			LOGF(LERR, "uc: Unexpected init_camera_list_t version %d: should be %d\n", p_data->header.version, INIT_CAMERA_LIST_MSG_VERSION);
		}
    }
	else {
		LOGF(LERR, "uc: Duplicate init_camera_list_t message, ignored\n");
	}
}

static void uc_msgcb_time_info(void *p_user, const ptc_msg_info_t *p_info)
{
    time_info_t  *p_data = (time_info_t *) p_info->p_data;

	LOGF(LSYS, "uc: va_time_info_t: %s\n", (uc_state & UC_HAS_TIME_INFO) ? "ignored" : "accepted");
    if (!(uc_state & UC_HAS_TIME_INFO)) {

		/* Verify message contents */
		PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_time_info_t);

		/* Add message-specific processing... */
		if (p_data->header.version == TIME_INFO_MSG_VERSION)
		{
			show_it(p_info);
			memcpy(&uc_time_info, p_data, sizeof(uc_time_info)) ;
			uc_state |= UC_HAS_TIME_INFO ;

			{
	        	static char ti_buf[128] = {0};
	        	char srcbuf[16] ;
	        	int rc ;
	        	extern int errno ;

	        	memset(srcbuf, 0, sizeof(srcbuf)) ;
	        	strncpy(srcbuf, p_data->time_source, TIME_SOURCE_LENGTH) ;

	        	sprintf(ti_buf, "timestamp=%u, time_offset=%lld, src='%s'",
	        		p_data->system_time, (long long) (p_data->time_offset), srcbuf) ;
				syslog(LOG_INFO, "%s: Time Info Message received: %s", __func__, ti_buf);

	        	rc = setenv("TIME_INFO", ti_buf, 1) ;
	        }
		}
		else {
			LOGF(LERR, "uc: Unexpected time_info_t version %d: should be %d\n", p_data->header.version, TIME_INFO_MSG_VERSION);
		}
    }
	else {
		LOGF(LERR, "uc: Duplicate time_info_t message, ignored\n");
	}
}

static void uc_msgcb_locomotive_id(void *p_user, const ptc_msg_info_t *p_info)
{
	locomotive_id_t  *p_data;
	char tmp_buff[32] = {0};
	int rc = 0;

	if ((p_info == NULL) || (p_info->p_data == NULL))
	{
		syslog(LOG_INFO, "%s: Bad parameter. Return", __func__);		
		return;
	}

	p_data = (locomotive_id_t *) p_info->p_data;
	LOGF(LSYS, "uc: locomotive_id_t: %s\n", (uc_state & UC_HAS_LOCOID) ? "ignored" : "accepted");
    if (!(uc_state & UC_HAS_LOCOID)) {

		/* Verify message contents */
		PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_locomotive_id_t);

		/* Add message-specific processing... */
		if (p_data->header.version == LOCOMOTIVE_ID_MSG_VERSION)
		{
			show_it(p_info);
			memcpy(&uc_loco_id, p_data, sizeof(uc_loco_id)) ;
			uc_state |= UC_HAS_LOCOID ;
			syslog(LOG_INFO, "%s:%s: Loco ID message received: %s", __FILE__, __func__, &uc_loco_id.locomotive_id[0]);
		}
		else {
			LOGF(LERR, "uc: Unexpected locomotive_id_t version %d: should be %d\n", p_data->header.version, LOCOMOTIVE_ID_MSG_VERSION);
		}
    }
	else {
		LOGF(LERR, "uc: Duplicate locomotive_id_t message, ignored\n");
	}
}

static void uc_msgcb_clear_uc(void *p_user, const ptc_msg_info_t *p_info)
{
	clear_user_config_t  *p_data = (clear_user_config_t *) p_info->p_data;

	LOGF(LSYS, "uc: clear_user_config_t\n");

	/* Verify message contents */
	PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_clear_user_config_t);

	/* Add message-specific processing... */
	if (p_data->header.version == CLEAR_USER_CONFIG_MSG_VERSION) {
		show_it(p_info);
		if (p_data->clear_camera_list) {
			memset(&uc_camera_list, 0, sizeof(uc_camera_list)) ;
			uc_state &= (~UC_HAS_CAMERA_LIST) ;
		}
		if (p_data->clear_timeoffset_info) {
			memset(&uc_time_info, 0, sizeof(uc_time_info)) ;
			uc_state &= (~UC_HAS_TIME_INFO) ;
		}
		if (p_data->clear_locoid_info) {
			memset(&uc_loco_id, 0, sizeof(uc_loco_id)) ;
			uc_state &= (~UC_HAS_LOCOID) ;
		}
	}
	else {
		LOGF(LERR, "uc: Unexpected clear_user_config_t version %d: should be %d\n", p_data->header.version, CLEAR_USER_CONFIG_MSG_VERSION);
	}
}

static void uc_msgcb_hb_request(void *p_user, const ptc_msg_info_t *p_info)
{
    heartbeat_request_t *p_data = (heartbeat_request_t *) p_info->p_data;

    show_it(p_info);

    LOGF(LSYS, "uc:va_heartbeat_request_t\n");

    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_heartbeat_request_t);

    /* Add message-specific processing... */
    if (p_data->header.version != HEARTBEAT_REQUEST_MSG_VERSION)
    {
        LOGF(LERR, "uc: Unexpected heartbeat_request_t version %d: should be %d\n", p_data->header.version, HEARTBEAT_REQUEST_MSG_VERSION);
    }
    else
    {
		static va_heartbeat_response_t msg = { { HEARTBEAT_RESPONSE_MSG_VERSION } };

		msg.payload.sequence = p_data->sequence;
		msg.payload.status   = HEARTBEAT_NORMAL;
		PTC_SEND_MSG_WITH_PAYLOAD(HEARTBEAT_RESPONSE, msg, va_heartbeat_response_t);
    }
}

static void ptc_uc_logmsg_cb(logsev_t sev, UINT8 enc, const CHAR *text, UINT32 text_len)
{
	int log_lev ;
	char *p ;

	switch(sev)
	{
		case LSYS:
		case LNOTE:
			log_lev = LOG_NOTICE ;
			break ;
		case LINFO:
			log_lev = LOG_INFO ;
			break ;
		case LERR:
			log_lev = LOG_ERR ;
			break ;
		case LWARN:
			log_lev = LOG_WARNING ;
			break ;
		case LDATA:
		case LMDATA:
		case LDEBUG:
			log_lev = LOG_DEBUG ;
			break ;
		default:
			log_lev = LOG_INFO ;
			break ;
	}

	p = (char *) text ;

	syslog(log_lev, "%s", p) ;
}

static void uc_handle_signal(int signal)
{
    // Find out which signal we're handling
    switch (signal)
    {
        case SIGTERM:
        case SIGINT:
        case SIGHUP:
        	fprintf(stderr, "SIGNAL #%d caught -- exiting ... \n", signal) ;
			syslog(LOG_ERR, "%s: SIGNAL #%d caught -- exiting ... \n", __func__, signal) ;			
         	break ;
        default:
        	break;
    }
}

int uc_collect()
{
	extern void ptc_send_msgids(void);
	int i ;

	if (!uc_is_ready()) {	/* can operate only when the UC is incomplete */
	    char name[] = "reagent_user_config_cli";
	    char *argv[2] = {
	    	"reagent",
			NULL
	    };
		keep_going = 1 ;

	    ptc_initialize(1, argv);
	    logger_add_msg_callback(ptc_uc_logmsg_cb, LOGFLG_NO_SEV_CB) ;
	    logger_set_flags(LOGFLG_NO_SEV_NAME | LOGFLG_NO_DATETIME | LOGFLG_NO_TIME_MS /* | LOGFLG_NO_NAME */) ;

	    if (ptc_connect(0, name) != SUCCESS)
	    {
	        LOG(LERR, "Failed to init ptc\n");
			syslog(LOG_ERR, "UC: failed to initialize PTC messaging\n") ;
	        return (EXIT_PTC_INIT_ERR);
	    }

		if (PTC_MSG_ATTACH_LIST(uc_msg_cb_list) == SUCCESS) {
			int uc_ttl = UC_TIMEOUT ;
			va_video_application_status_t msg = {0} ;

			signal(SIGTERM, uc_handle_signal);
			signal(SIGINT, uc_handle_signal);
			signal(SIGHUP, uc_handle_signal);

			syslog(LOG_INFO, "UC: entered User Configuration collection loop\n") ;


		    ptc_send_msgids();
		    ptcapi.sent_msgid_list = TRUE;

			memset(&msg, 0, sizeof(va_video_application_status_t));
			// Set Download Status Message variables
			msg.payload.header.version = VIDEO_APPLICATION_STATUS_MSG_VERSION;
			msg.payload.configuration_info_available = 1 ;
			// Send Video App Status
			PTC_SEND_MSG_WITH_PAYLOAD(VIDEO_APPLICATION_STATUS, msg, va_video_application_status_t);

			while ((keep_going) && ((--uc_ttl) > 0)) {
				ptc_poll() ;

				memset(&msg, 0, sizeof(va_video_application_status_t));
				// Set Download Status Message variables
				msg.payload.header.version = VIDEO_APPLICATION_STATUS_MSG_VERSION;
				msg.payload.configuration_info_available = 1 /*(int) uc_is_ready()*/;
				// Send Video App Status
				////PTC_SEND_MSG_WITH_PAYLOAD(VIDEO_APPLICATION_STATUS, msg, va_video_application_status_t);

				/* exit the UC loop if the UC is fully collected */
				if (uc_is_ready()) {
					break ;
				}

				sleep(1) ;
#ifdef __aarch64__
				uc_ttl = 1 ;	/* don't wait longer than a second if running on NAS-vdock */
#endif
			}

			logger_del_msg_callback(ptc_uc_logmsg_cb) ;
			for (i=0; i < sizeof(uc_msg_cb_list)/sizeof(ptc_msg_callback_entry_t); i++) {
				ptc_msg_detach(uc_msg_cb_list[i].msgid, uc_msg_cb_list[i].p_callback, NULL) ;
			}
			ptc_stop() ;
			////ptc_poll() ;

			if (uc_ttl <= 0) {
				syslog(LOG_WARNING, "UC: could not fully collect User Configuration in %d sec -- %02X\n", UC_TIMEOUT, uc_get_state()) ;
			}
		}
		else {
			fprintf(stderr, "uc: cannot attach\n") ;
			syslog(LOG_ERR, "UC: cannot attach PTC MSG callback list\n") ;
		}
		keep_going = 0 ;
	}
	else {
		syslog(LOG_ERR, "UC_COLLECT -- User Configuration is already collected -- ignoring\n") ;
	}

	signal(SIGTERM, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGHUP, SIG_DFL);


	return (uc_is_ready()) ;
}

int uc_get_state()
{
	return (uc_state & UC_MASK) ;
}

int uc_is_ready()
{
	return ((uc_state & UC_MASK) == UC_MASK) ;
}

char * uc_get_locoid()
{
	static time_info_t uc_time_info ;
	return (&(uc_loco_id.locomotive_id[0])) ;
}

camera_list_element_t * uc_get_camera_list()
{
	return (&(uc_camera_list[0])) ;
}

time_info_t * uc_get_time_info()
{
	return (&uc_time_info) ;
}

ready_status_t * uc_get_ready_status()
{
	return (& uc_ready_status) ;
}
