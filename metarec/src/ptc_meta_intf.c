#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <syslog.h>

#include "ptc_config.h"

#include "crc_utils.h"
#include "logger.h"
#include "ptc_api.h"
#include "video_api.h"

#include "metaintf.h"
#include "metaio.h"
#include "metadata_util.h"
#include "ptc_meta_intf.h"


static volatile int commanded_recording_mode = 0 ;
static pthread_mutex_t ptci_data_mutex = PTHREAD_MUTEX_INITIALIZER ;

/* static function prototyes */
static void msgcb_time_info_meta_rec(void *p_user, const ptc_msg_info_t *p_info);
static void msgcb_video_event_info_meta_rec(void *p_user, const ptc_msg_info_t *p_info);
static void msgcb_gps_data_meta_rec(void *p_user, const ptc_msg_info_t *p_info);
static void msgcb_digital_config_meta_rec(void *p_user, const ptc_msg_info_t *p_info);
static void msgcb_shutdown_meta_rec(void *p_user, const ptc_msg_info_t *p_info);
static void msgcb_ready_status_meta_rec(void *p_user, const ptc_msg_info_t *p_info);
static void msgcb_locomotive_id(void *p_user, const ptc_msg_info_t *p_info);

/* interested ptc messages and its message handler */
static const ptc_msg_callback_entry_t meta_msg_cb_list[] =
{
    { TIME_INFO                   , msgcb_time_info_meta_rec },
    { LOCOMOTIVE_ID               , msgcb_locomotive_id},
    { VIDEO_EVENT_INFO            , msgcb_video_event_info_meta_rec },
    { GPS_DATA                    , msgcb_gps_data_meta_rec },
    { DIGITAL_INPUTS              , msgcb_digital_config_meta_rec }, 
    { SHUTDOWN                    , msgcb_shutdown_meta_rec }, 
    { READY_STATUS                , msgcb_ready_status_meta_rec }    
};


/**
 * Process Time Info Message from video_rec and update local variable
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
void msgcb_time_info_meta_rec(void *p_user, const ptc_msg_info_t *p_info)
{   
    time_info_t *p_data = (time_info_t *) p_info->p_data;

    static int64_t previous_time_offset = 0;

    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_time_info_t);

    /* Add message-specific processing... */
    if (p_data->header.version != TIME_INFO_MSG_VERSION)
    {
        syslog(LOG_NOTICE, "%s: Unexpected time_info_t version %d: should be %d", __func__, p_data->header.version, TIME_INFO_MSG_VERSION);
    }
    else
    {   
    	if (p_data->time_source[0] != '\0')
    	{
    		set_time_info(p_data->system_time, p_data->time_offset, p_data->time_source);

            // In the case of a Forward Time Change, metarec will need to be informed of this
            // change so that a new chunk can be created
            if ((previous_time_offset <  0) &&   // Previous Time Offset was a negative value
                (p_data->time_offset  == 0)    ) // New Time Offset is zero (Forward Time Change)
            {
                update_forward_time_change_status(true);
            }

            // Store new time offset as previous
            previous_time_offset = p_data->time_offset;
    	}
	}
} //msgcb_time_info_meta_rec

/**
 * Process Loco ID Message from video_rec and update metadata 
 * 
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_locomotive_id(void *p_user, const ptc_msg_info_t *p_info)
{
	locomotive_id_t *p_data = (locomotive_id_t *) p_info->p_data;
    int  locoIdSize = 0;

    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_locomotive_id_t);

    /* Add message-specific processing... */
    if (p_data->header.version != LOCOMOTIVE_ID_MSG_VERSION)
    {
        syslog(LOG_NOTICE, "%s: Unexpected locomotive_id_t version %d: should be %d", __func__, p_data->header.version, LOCOMOTIVE_ID_MSG_VERSION);
    }
    else
    {
        if (p_data->locomotive_id[0] != '\0')
        {
		    syslog(LOG_INFO, "%s: Loco ID message received: %s", __func__, p_data->locomotive_id);
            set_locomotive_id(p_data->locomotive_id);
        }

	}
} //msgcb_locomotive_id

/**
 * Process Video event Message from video_rec and update local variable
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
void msgcb_video_event_info_meta_rec(void *p_user, const ptc_msg_info_t *p_info)
{
    video_event_info_t  *p_data = (video_event_info_t *) p_info->p_data;

    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_video_event_info_t);

	if (p_data != NULL)
	{
			/* Add message-specific processing... */
			if (p_data->header.version != VIDEO_EVENT_INFO_MSG_VERSION)
			{
				syslog(LOG_NOTICE, "%s: Unexpected video_event_info_t version %d: should be %d", __func__, p_data->header.version, VIDEO_EVENT_INFO_MSG_VERSION);
			}
			else
			{         
				if (p_data->name[0] != '\0')
				{                
					syslog(LOG_NOTICE, "%s: Event received: name %s, value %d", __func__, p_data->name, p_data->value); 
					set_video_event(p_data->name, p_data->value);        	
				}               
			}
	}
} //msgcb_video_event_info_meta_rec

/**
 * Process GPS Message from video_rec and update local variable
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
void msgcb_gps_data_meta_rec(void *p_user, const ptc_msg_info_t *p_info)
{
    gps_data_t *p_data = (gps_data_t *) p_info->p_data;

    int32_t latitude, longitude, speed = 0;
    char lat_d = 0 , lon_d = 0;

    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_gps_data_t);

    /* Add message-specific processing... */
    if (p_data->header.version != GPS_DATA_MSG_VERSION)
    {
        syslog(LOG_NOTICE, "%s: Unexpected gps_data_t version %d: should be %d", __func__, p_data->header.version, GPS_DATA_MSG_VERSION);
    }
    else
    {   
		/*fetch the directions*/
        lat_d = get_gps_direction(p_data->latitude);
        lon_d = get_gps_direction(p_data->longitude);
        
        latitude = parse_gps_coordinate(p_data->latitude, eGPSLatitude);
        longitude = parse_gps_coordinate(p_data->longitude, eGPSLongitude);        
        
        speed = parse_gps_speed(p_data->speed); 

        if (!(latitude == 0 && longitude == 0 && speed == 0))
        {
			/*If the direction is South for latitude or West for longitude, 
			apply a negative sign to the coordinate so that the player can distinguish between the two.*/
            if ((lat_d == 's' || lat_d == 'S') && latitude > 0)
                latitude = latitude * -1;
            if ( (lon_d == 'w' || lon_d == 'W') && longitude > 0)
                longitude = longitude * -1;
            
        	set_gps(latitude,longitude, speed);
        }        
	}
} //msgcb_gps_data_meta_rec


/**
 * Process digital configuration Message from video_rec and update the local mapping
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
void msgcb_digital_config_meta_rec(void *p_user, const ptc_msg_info_t *p_info)
{
	int idx = 0;
    digital_inputs_t *p_data = (digital_inputs_t *) p_info->p_data;
    digital_input_descriptor_t *p_digital_input = NULL;
   
    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_digital_inputs_t);
    if (p_data != NULL)
    {
        /* Add message-specific processing... */
        if (p_data->header.version != DIGITAL_INPUTS_MSG_VERSION)
        {
            syslog(LOG_NOTICE, "%s: Unexpected digital_inputs_t version %d: should be %d", __func__, p_data->header.version, DIGITAL_INPUTS_MSG_VERSION);
        }
        else
        {          
            p_digital_input = &((p_data->di)[0]);

            for( idx = 0; idx < NUM_DIGITAL_INPUTS; ++idx, ++p_digital_input)
            {
                if (p_digital_input != NULL && p_digital_input->di_number != 0)
                {
                    syslog(LOG_NOTICE, "%s: Event received: name %s, number %d, recording %d", __func__, p_digital_input->di_name, p_digital_input->di_number, p_digital_input->di_recording);   
                    set_digital_input_config(p_digital_input->di_name, p_digital_input->di_number, p_digital_input->di_recording);   
                }
            }   
        }
    }
} //msgcb_digital_config_meta_rec

/**
 * Process Ready Status Message from video_rec
 * to determine where metadata can be recorded to
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_ready_status_meta_rec(void *p_user, const ptc_msg_info_t *p_info)
{
    ready_status_t *p_data   = (ready_status_t *) p_info->p_data;

    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_ready_status_t);

    /* Add message-specific processing... */
    if (p_data->header.version == READY_STATUS_MSG_VERSION)
    {
        int rm = -1 ;

        if (ptci_data_lock() == 0) {
            rm = commanded_recording_mode ;
            ptci_data_unlock() ;
        }

        if (rm != -1) {
            rm = 0 ;
            if (p_data->chm_ready) {
                rm |= DO_RECORD_CHM_META ;
            }
            if (p_data->removable_ssd_ready) {
                rm |= DO_RECORD_SSD_META ;
            }
            syslog(LOG_INFO, "%s: commanded recording mode id %02X", __func__, rm)  ;
        }
        else {
            syslog(LOG_ERR, "%s: cannot lock PTCI data, recording mode forced to 0", __func__)  ;
            rm = 0 ;
        }

        commanded_recording_mode = rm ;
    }
    else {
        syslog(LOG_ERR, "%s: Unexpected ready_status_t version %d: should be %d", __func__, p_data->header.version, READY_STATUS_MSG_VERSION);
    }
} //msgcb_ready_status_meta_rec


/**
 * Process SHUTDOWN Message from video_rec
 * to shutdown recording on either CHM or SSD
 *
 * @param[in] p_user         - Not used
 * @param[in] p_info         - Incoming message bytes message structure
 *
 * @return    None
 */
static void msgcb_shutdown_meta_rec(void *p_user, const ptc_msg_info_t *p_info)
{
    shutdown_t *p_data   = (shutdown_t *) p_info->p_data;

    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_shutdown_t);

    /* Add message-specific processing... */
    if (p_data->header.version == SHUTDOWN_MSG_VERSION)
    {
        int rm = -1 ;

        if (ptci_data_lock() == 0) {
            rm = commanded_recording_mode ;
            ptci_data_unlock() ;
        }

        if (rm != -1) {
            switch (p_data->function_type)
            {
            case SHUTDOWN_CHM:
                rm &= (~(DO_RECORD_CHM_META)) ;
                break;
            case SHUTDOWN_REMOVABLE_SSD:
                rm &= (~(DO_RECORD_SSD_META)) ;
                break;
            case SHUTDOWN_ALL:
                rm &= (~(DO_RECORD_CHM_META | DO_RECORD_SSD_META)) ;
                break;
            }
            commanded_recording_mode = rm ;
            syslog(LOG_INFO, "%s: commanded recording mode id %02X", __func__, rm)  ;
        }
    }
    else {
        syslog(LOG_ERR, "%s: Unexpected shutdown_t version %d: should be %d", __func__, p_data->header.version, SHUTDOWN_MSG_VERSION);
    }
} //msgcb_shutdown_meta_rec

/*
 * bring PTC interface up. Return nonzero on success
 */
int ptci_up_meta(int argc, char *argv[], char *name)
{
    int rc = 0 ;

    /* initialize the PTC client here and subscribe to the messages of interest */
    ptc_initialize(argc, argv);

    // Connect to ptc router
    if (ptc_connect(0, name) == SUCCESS){
        if (PTC_MSG_ATTACH_LIST(meta_msg_cb_list) == SUCCESS) {
            rc = 1 ;
        }
        else {
            ptc_stop() ;
            rc = 0 ;
            syslog(LOG_ERR, "%s: Failed to attach the PTC message callback list", __func__);
        }
    }
    else {
        syslog(LOG_ERR, "%s: Unable to connect to PTC Router", __func__);
    }

    return rc ;
}

/*
 * on success returns current recording mode, as commanded, i.e. based on drives ready status (cannot be greater than 0x1f)
 * returns -1 on failure (ptci data mutex cannot be locked)
 */
int ptci_get_rm_meta()
{
    int rc = -1 ; ;

    if (ptci_data_lock() == 0) {
        rc = commanded_recording_mode ;
        ptci_data_unlock() ;
    }

    return rc ;
}

/*
 * update the commanded recording mode upon initialization, value is based on the reagent config
 */
void ptci_set_rm_meta(int rm)
{
    if (ptci_data_lock() == 0) {
        commanded_recording_mode = rm ;
        ptci_data_unlock() ;
    }
}

/*
 * mimics the behavior of pthread_mutex_lock()
 * returs 0 on success, nonzero otherwise
 */
int ptci_data_lock()
{
    int rc ;

    rc = pthread_mutex_lock(&ptci_data_mutex) ;

    return rc ;
}

void ptci_data_unlock()
{
    pthread_mutex_unlock(&ptci_data_mutex) ;
}
