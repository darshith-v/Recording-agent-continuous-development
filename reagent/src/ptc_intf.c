/**
 * @file    ptc_intf.c
 *
 * @author  WRE SW Engineer
 *
 * @section DESCRIPTION
 *
 * Functions needed to process Video API messages
 *
 * @section COPYRIGHT
 *
 * Copyright 2019-2020 WABTEC Railway Electronics
 * This program is the property of WRE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <stdlib.h>
#include <signal.h>
#include <syslog.h>

#include "crc_utils.h"
#include "exit_codes.h"

#include "ra_download.h"
#include "reagent_config.h"
#include "luaif.h"
#include "domutil.h"

#include "groom_util.h"

#ifndef OS_TGT_LINUX
#define OS_TGT_LINUX
#endif

#include "ptc_config.h"

#include "logger.h"
#include "ptc_api.h"
#include "video_api.h"
#include "ptc_intf.h"
#include "camon.h"
#include "ovconfig.h"

#define CAMERA_RESET_SLEEP 	500000
#define IP_ADD_LENGTH 30
#define START_TIMER_LIMIT 10

typedef enum
{
    HB_GOOD_RESPONSE = 1,
    HB_NO_RESPONSE,
    HB_INVALID_SEQUENCE,
    HB_RESTART,
    HB_QUIT
} hbResponderMode_e;


/* this is supposed to be defined in camera_control.h . Not ready yet as of 03/23/2020 */
#if 1
typedef enum
{
    NO_ERROR = 0,
    ERROR_CONNECTION_FAILURE = -1,
    ERROR_MALLOC_FAILURE = -2,
    ERROR_HTTPS_NOT_SUPPORTED = -3,
    ERROR_RECIVED_TIMEOUT = -4,
    ERROR_INVALID_CONTENT = -5,
    ERROR_NULL_CONTENT = -6,
    ERROR_PARSE_FAILURE = -7,
    ERROR_HANDLE_FAILURE = -8,
    ERROR_HTTP_RESPONSE = -9,
    ERROR_AUTHENTICATION_FAILURE = -10,
} RE_ERROR;

#endif

enum
{
	e_pause = 0,
	e_record = 1
};

enum
{
	e_chm = 0,
	e_rssd = 1
};

enum
{
	e_not_available = 0,
	e_available = 1
};
// Externs

extern void update_config_files(char *locoid);

static int rp_ttl[MAX_CAMS] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
static hbResponderMode_e g_mode;

/* 'Extra LUA state' pointer, returned by http_start() and used to convey
 * the runtime configuration data to the HTTP server
 */
static lua_State *g_extra_lstate = NULL ;

// Function Prototypes
static int   get_responder_mode         (void);
static int   handle_camera_record_pause(camera_record_pause_t *p_data) ;
static void  camera_recording_just_stopped(media_info_t *p_minfo, camdef_t *p_camdef) ;
static pid_t spawn_process(int argc, char **argv);
static void  handle_space_monitor(media_info_t *p_minfo);
static void  handle_metarec(media_info_t *p_minfo);

int  start_recording_process(media_info_t *p_minfo, camdef_t *p_camdef);
int  stop_recording_process(media_info_t *p_minfo, camdef_t *p_camdef);
void send_camera_status(camdef_t *p_camdef);
int  get_assigned_camera_count(media_info_t *p_minfo);
static void stop_space_monitor(media_info_t *p_minfo);

static int startTimer = 0;
static BOOL shutdownInProcess = FALSE;

#ifdef DEBUG_PTC_MESSAGING

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
    "DIGITAL_INPUTS",
};

static char *show_ptc_addr              (const ptc_addr_t     *paddr,
                                         char                 *buf);
static void  pktdump                    (char                 *phdr, 
                                         void                 *pdata,
                                         int                   n);
static void  show_it                    (const ptc_msg_info_t *p_info);
#endif

static void         send_download_ack          (uint64_t              file_size);
static void         send_download_status       (uint8_t               status);
static void         send_heartbeat_response_msg(uint8_t               sequence,
                                         heartbeat_status_e    status);
static void         sendCameraStatus           (camera_status_t       cameraStatus);
static int 			see_if_recording		   (camdef_t *p_camdef, 
												int ncams);
static void         processConfigError         (int                   errorNumber,
                                         uint32_t              msgType);
static void         _10Hz_timer_callback       (timer_id_t            tid,
                                         INT32                 xcount,
                                         void                 *p_user);
static void         msgcb_time_info            (void                 *p_user,
                                         const ptc_msg_info_t *p_info);
static void         msgcb_locomotive_id        (void                 *p_user,
                                         const ptc_msg_info_t *p_info);

static void         msgcb_camera_settings      (void                 *p_user,
                                         const ptc_msg_info_t *p_info);
static void         msgcb_camera_resolution    (void                 *p_user,
                                         const ptc_msg_info_t *p_info);
static void         msgcb_camera_compression   (void                 *p_user,
                                         const ptc_msg_info_t *p_info);
static void         msgcb_camera_frame_rate    (void                 *p_user,
                                         const ptc_msg_info_t *p_info);
static void         msgcb_camera_record_pause  (void                 *p_user,
                                         const ptc_msg_info_t *p_info);
static void         msgcb_ready_status         (void                 *p_user,
                                         const ptc_msg_info_t *p_info);
static void         msgcb_download             (void                 *p_user,
                                         const ptc_msg_info_t *p_info);
static void         msgcb_heartbeat_request    (void                 *p_user,
                                         const ptc_msg_info_t *p_info);
static void         msgcb_accessory_power      (void                 *p_user,
                                         const ptc_msg_info_t *p_info);
static void         msgcb_request_camera_status(void                 *p_user,
                                         const ptc_msg_info_t *p_info);
static void         msgcb_camera_snapshot      (void                 *p_user,
                                         const ptc_msg_info_t *p_info);
static void         msgcb_camera_profile       (void                 *p_user,
                                         const ptc_msg_info_t *p_info);

static void msgcb_shutdown_rec(void *p_user, const ptc_msg_info_t *p_info);


static int reset_camera_stream(char *camera_name);
// Timers
static timer_id_t _10Hz_timer;
static timer_id_t _1Hz_timer;

// Static variables
static bool isRestartRequired = false; // Flag to denote that a restart of the Recording Agent is required

/* removes trailing '_'s from camera name, loco_id ... */
static void trunc_name(char *pname, int size)
{
	char *p = NULL;

	if ((pname != NULL) && (size > 0)) {
		p = pname + (size-1) ;
		while (p > pname) {
			if ((*p=='_') || (*p<=' ')) {
				*p = '\0' ;
				--p ;
			}
			else {
				break ;
			}
		}
	}
}

static void ptc_logmsg_cb(logsev_t sev, UINT8 enc, const CHAR *text, UINT32 text_len)
{
	int log_lev = 0;
	char *p = NULL;
	const char *namestr="reagent:" ;

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

	if ((p=strstr(text, namestr)) != NULL) {
		p += strlen(namestr) ;
	}
	else {
		p = (char *) text ;
	}

	syslog(log_lev, "%s", p) ;
}

/**
 * Function to obtain Overall Health of Recording Agent
 * for the purpose of populating the Heartbeat Response Message
 *
 * @param[in] None
 *
 * @return    Overall Health using enumerated values from hbResponderMode_e
 */
static int get_responder_mode(void)
{
    //TODO: Get Overall Health of Recording Agent then send APPROPRIATE response.
    if (true == isRestartRequired)
    {
        return (HB_RESTART);
    }
    else
    {
        return (HB_GOOD_RESPONSE);
    }    
} //get_responder_mode

#ifdef DEBUG_PTC_MESSAGING
/**
 * Function to obtain PTC Message Address
 *
 * @param[in] *paddr Pointer to address
 * @param[in] *buf   Pointer to buffer
 *
 * @return     Address
 */
static char *show_ptc_addr(const ptc_addr_t *paddr, char *buf)
{
	char *p = buf;
	int   i = 0;

	if (paddr)
	{
		if (paddr->type == PTC_ADDR_TYPE_SRC)
		{
			strcpy(p, "src: ");
		}
		else
		{
			strcpy(p, "dst: ");
		}
		p += 5;

		for (i = 0; i < paddr->length; i++)
		{
			if ((paddr->data[i] > ' ') && (paddr->data[i]<0x7f))
			{
				*p++ = paddr->data[i];
			}
			else
			{
				sprintf(p, "\\%02X", (unsigned char) paddr->data[i]);
				p += 3;
			}
		}
	}
	else
	{
		strcpy(p, "NULL");
	}
	return (buf);
} //show_ptc_addr

/**
 * Helper Function
 *
 * @param[in] *phdr  Pointer to header
 * @param[in] *pdata Pointer to data
 * @param[in]  n     Number of bytes to process
 *
 * @return     None
 */
static void pktdump(char *phdr, void *pdata, int n)
{
	unsigned char *p     = (unsigned char *) pdata;
	unsigned char *pold;
	int            off   = 0;
	int            i     = 0;

	if (phdr)
	{
		fprintf(stderr,"%s, len=%d bytes\n", phdr, n);
	}

	pold = p;
	fprintf(stderr,"   %04X: ", off);

	for (i = 0; i < n ; i++)
	{
		fprintf(stderr,"%02X ", *p);
		++p;
		++off;

		if ((p - pold) >= 8)
		{
			fprintf(stderr,"   | ");

			while (pold < p)
			{
				fprintf(stderr,"%c ", ((*pold >= ' ') && (*pold <= 0x7f)) ? *pold : '.' );
				++pold;
			}
			fprintf(stderr," |\n");
			fprintf(stderr,"   %04X: ", off);
		}
	}

	if ((n = p - pold) > 0)
	{
		for (i = 0; i < (8 - n); i++)
		{
			fprintf(stderr,"   ");
		}
		fprintf(stderr,"   | ");

		while (pold < p)
		{
			fprintf(stderr,"%c ", ((*pold >= ' ') && (*pold <= 0x7f)) ? *pold : '.' );
			++pold ;
		}

		for (i = 0; i < (8 - n); i++)
		{
			fprintf(stderr,"  ");
		}
		fprintf(stderr," |\n");
	}

	fprintf(stderr,"\n") ;
} //pktdump

/**
 * Helper Function to display messages
 *
 * @param[in] *p_info Pointer to PTC message payload
 *
 * @return     None
 */
static void show_it(const ptc_msg_info_t *p_info)
{
	uint32_t idx = 0;
	char     srcbuf[64], dstbuf[64];

	idx = p_info->msgid - (TIME_INFO);
	fprintf(stderr,"%s: %d bytes %s => %s\n",
		    msg_names[idx], p_info->data_len, show_ptc_addr(p_info->p_src, &srcbuf[0]), show_ptc_addr(p_info->p_dest, &dstbuf[0]));

	pktdump("payload", (void *) (p_info->p_data), (int)(p_info->data_len));
	fflush(stdout);
} //show_it
#else
#define show_it(x)
#endif


/**
 * Function to send the Download Ack to video_rec
 *
 * @param[in] file_size - Size of the file to be downloaded
 *
 * @return    None
 */
static void send_download_ack(uint64_t file_size)
{
    va_download_ack_t downloadAckMessage;

    memset(&downloadAckMessage, 0, sizeof(va_download_ack_t));

    // Set Download Ack Message variables
    downloadAckMessage.payload.header.version = DOWNLOAD_ACK_MSG_VERSION;
    downloadAckMessage.payload.file_size      = file_size;

    // Send Download Ack
    PTC_SEND_MSG_WITH_PAYLOAD(DOWNLOAD_ACK, downloadAckMessage, va_download_ack_t);
    LOGF(LDEBUG, "%s: Send Download Ack to video_rec process. File Size: %lu\n", __func__, (unsigned long)downloadAckMessage.payload.file_size);

} //send_download_ack

/**
 * Function to send the Download Status to video_rec
 *
 * @param[in] status - Status of the download request
 *
 * @return    None
 */
static void send_download_status(uint8_t status)
{
    va_download_status_t downloadStatusMessage;

    memset(&downloadStatusMessage, 0, sizeof(va_download_status_t));

    // Set Download Status Message variables
    downloadStatusMessage.payload.header.version = DOWNLOAD_STATUS_MSG_VERSION;
    downloadStatusMessage.payload.status         = status;

    // Send Download Status
    PTC_SEND_MSG_WITH_PAYLOAD(DOWNLOAD_STATUS, downloadStatusMessage, va_download_status_t);
    LOGF(LDEBUG,"%s: Send Download Status to video_rec process. Status: %d\n",__func__, downloadStatusMessage.payload.status);

} //send_download_status

/**
 * Transmits heartbeat response message.
 *
 * @param[in] sequence - Sequence number to put into message payload
 * @param[in] status   - Status value to put into message payload
 *
 * @return    None
 */
static void send_heartbeat_response_msg(uint8_t sequence, heartbeat_status_e status)
{
    static va_heartbeat_response_t msg = { { HEARTBEAT_RESPONSE_MSG_VERSION } };

    msg.payload.sequence = sequence;
    msg.payload.status   = status;

    ////LOGF(LSYS, "sending va_heartbeat_response_t\n");
    PTC_SEND_MSG_WITH_PAYLOAD(HEARTBEAT_RESPONSE, msg, va_heartbeat_response_t);

} //send_heartbeat_response_msg

/**
 * Function to send the Camera Status Message to video_rec
 *
 * @param[in] cameraStatus - Structure containing the Camera Info
 *
 * @return    None
 */
static void sendCameraStatus(camera_status_t cameraStatus)
{
    va_camera_status_t cameraStatusMessage;
#ifdef DEBUG    
    char temp[CAMERA_NAME_LENGTH + 4] = {0};    // arbitrary storage
#endif

    memset(&cameraStatusMessage, 0, sizeof(va_camera_status_t));

    cameraStatusMessage.payload.header.version     = CAMERA_STATUS_MSG_VERSION;

    // Set Camera Status Message variables
    strncpy(cameraStatusMessage.payload.camera, cameraStatus.camera, CAMERA_NAME_LENGTH);
    strncpy((uint8_t *) cameraStatusMessage.payload.camera_serial, (uint8_t *) cameraStatus.camera_serial, CAMERA_SERIAL_NUM_LENGTH);
    strncpy(cameraStatusMessage.payload.camera_model, cameraStatus.camera_model, CAMERA_MODEL_INFO_LENGTH);
    
    cameraStatusMessage.payload.resolution         = cameraStatus.resolution;
    cameraStatusMessage.payload.compression        = cameraStatus.compression;
    cameraStatusMessage.payload.frame_rate         = cameraStatus.frame_rate;
    
    strncpy(cameraStatusMessage.payload.actual_resolution, cameraStatus.actual_resolution, CAMERA_RESOLUTION_LENGTH);
	
    cameraStatusMessage.payload.actual_compression = cameraStatus.actual_compression;
    cameraStatusMessage.payload.actual_framerate   = cameraStatus.actual_framerate;

    cameraStatusMessage.payload.no_audio_recording = cameraStatus.no_audio_recording;
    cameraStatusMessage.payload.no_chm_recording   = cameraStatus.no_chm_recording;
    cameraStatusMessage.payload.accessory_power_on = cameraStatus.accessory_power_on;
    cameraStatusMessage.payload.recording          = cameraStatus.recording;
    cameraStatusMessage.payload.camera_online      = cameraStatus.camera_online;
    cameraStatusMessage.payload.camera_assignment  = cameraStatus.camera_assignment;

#if DEBUG
    memset(temp, 0, sizeof(temp));
    strncpy(temp, cameraStatusMessage.payload.camera, CAMERA_NAME_LENGTH);   // Note camera length can be 12 characters.
    syslog(LOG_NOTICE, "%s: Camera %s", __func__, temp);
    syslog(LOG_NOTICE, "%s: Camera serial %s", __func__, cameraStatusMessage.payload.camera_serial);
    syslog(LOG_NOTICE, "%s: Camera model %s", __func__, cameraStatusMessage.payload.camera_model);
    
    syslog(LOG_NOTICE, "%s: Camera resolution %d", __func__, cameraStatusMessage.payload.resolution);
    syslog(LOG_NOTICE, "%s: Camera compression %d", __func__, cameraStatusMessage.payload.compression); 
    syslog(LOG_NOTICE, "%s: Camera frame rate %d", __func__, cameraStatusMessage.payload.frame_rate);
    
    syslog(LOG_NOTICE, "%s: Camera actual resolution %s", __func__, cameraStatusMessage.payload.actual_resolution);
    syslog(LOG_NOTICE, "%s: Camera actual compression %d", __func__, cameraStatusMessage.payload.actual_compression); 
    syslog(LOG_NOTICE, "%s: Camera actual framerate %d", __func__, cameraStatusMessage.payload.actual_framerate); 

    syslog(LOG_NOTICE, "%s: Camera no audio recording %d", __func__, cameraStatusMessage.payload.no_audio_recording); 
    syslog(LOG_NOTICE, "%s: Camera no chm recording %d", __func__, cameraStatusMessage.payload.no_chm_recording); 
    syslog(LOG_NOTICE, "%s: Camera sccessory power on %d", __func__, cameraStatusMessage.payload.accessory_power_on); 
    syslog(LOG_NOTICE, "%s: Camera recording %d", __func__, cameraStatusMessage.payload.recording); 
    syslog(LOG_NOTICE, "%s: Camera online %d", __func__, cameraStatusMessage.payload.camera_online);
    syslog(LOG_NOTICE, "%s: Camera assigned %d", __func__, cameraStatusMessage.payload.camera_assignment);
#else
	syslog(LOG_INFO, "CAMERA_STATUS '%s': rcf %d %d %d | acutal rcf %s %d %d | no_audio %d | no_chm_rec %d | accessory_power %d | recording %d | online %d | assigned %d", 
       	cameraStatus.camera,
        cameraStatus.resolution,
        cameraStatus.compression,
        cameraStatus.frame_rate,
        cameraStatus.actual_resolution,
        cameraStatus.actual_compression,
        cameraStatus.actual_framerate,
        cameraStatus.no_audio_recording,
        cameraStatus.no_chm_recording,
        cameraStatus.accessory_power_on,
        cameraStatus.recording,
        cameraStatus.camera_online,
        cameraStatus.camera_assignment);
	
#endif


    // Send Camera Status
    PTC_SEND_MSG_WITH_PAYLOAD(CAMERA_STATUS, cameraStatusMessage, va_camera_status_t);

} //sendCameraStatus

/**
 * Function to process Camera Configuration Error messages received and select
 * which Application Error Callback command to send to the Event Recorder
 *
 * @param[in] errorNumber Error Number recieved from function call
 *
 * @return    None
 */
static void processConfigError(int errorNumber, uint32_t msgType)
{
    switch (errorNumber)
    {
        case (NO_ERROR):
        {
            // No Error
            send_video_application_error(eNoError);
            break;
        }
        case (ERROR_CONNECTION_FAILURE):
            switch (msgType)
            {
                case (CAMERA_SETTINGS):
                    // Unable to set Camera Settings, connection not available
                    send_video_application_error(eCameraOffline);
                    send_video_application_error(eCameraSettingsFailure);
                    break;
                case (CAMERA_RESOLUTION):
                    // Unable to set Camera Resolution, connection not available
                    send_video_application_error(eCameraOffline);
                    send_video_application_error(eSetResolutionFailure);
                    break;
                case (CAMERA_COMPRESSION):
                    // Unable to set Camera Compression, connection not available
                    send_video_application_error(eCameraOffline);
                    send_video_application_error(eSetCompressionFailure);
                    break;
                case (CAMERA_FRAME_RATE):
                    // Unable to set Camera Frame Rate, connection not available
                    send_video_application_error(eCameraOffline);
                    send_video_application_error(eSetFrameRateFailure);
                    break;
            } // end switch (msgType)
        	break ;
        case (ERROR_MALLOC_FAILURE):
            switch (msgType)
            {
                case (CAMERA_SETTINGS):
                    // Unable to set Camera Settings, internal error
                    send_video_application_error(eCameraSettingsFailure);
                    break;
                case (CAMERA_RESOLUTION):
                    // Unable to set Camera Resolution, internal error
                    send_video_application_error(eSetResolutionFailure);
                    break;
                case (CAMERA_COMPRESSION):
                    // Unable to set Camera Compression, internal error
                    send_video_application_error(eSetCompressionFailure);
                    break;
                case (CAMERA_FRAME_RATE):
                    // Unable to set Camera Frame Rate, internal error
                    send_video_application_error(eSetFrameRateFailure);
                    break;
            } // end switch (msgType)
        	break ;
        case (ERROR_HTTPS_NOT_SUPPORTED):
            switch (msgType)
            {
                case (CAMERA_SETTINGS):
                    // Unable to set Camera Settings, internal error
                    send_video_application_error(eCameraSettingsFailure);
                    break;
                case (CAMERA_RESOLUTION):
                    // Unable to set Camera Resolution, internal error
                    send_video_application_error(eSetResolutionFailure);
                    break;
                case (CAMERA_COMPRESSION):
                    // Unable to set Camera Compression, internal error
                    send_video_application_error(eSetCompressionFailure);
                    break;
                case (CAMERA_FRAME_RATE):
                    // Unable to set Camera Frame Rate, internal error
                    send_video_application_error(eSetFrameRateFailure);
                    break;
            } // end switch (msgType)
        	break ;
        case (ERROR_RECIVED_TIMEOUT):
            switch (msgType)
            {
                case (CAMERA_SETTINGS):
                    // Unable to set Camera Settings, command timeout before response was received
                    send_video_application_error(eCameraNoDataRecvd);
                    send_video_application_error(eCameraSettingsFailure);
                    break;
                case (CAMERA_RESOLUTION):
                    // Unable to set Camera Resolution, command timeout before response was received
                    send_video_application_error(eCameraNoDataRecvd);
                    send_video_application_error(eSetResolutionFailure);
                    break;
                case (CAMERA_COMPRESSION):
                    // Unable to set Camera Compression, command timeout before response was received
                    send_video_application_error(eCameraNoDataRecvd);
                    send_video_application_error(eSetCompressionFailure);
                    break;
                case (CAMERA_FRAME_RATE):
                    // Unable to set Camera Frame Rate, command timeout before response was received
                    send_video_application_error(eCameraNoDataRecvd);
                    send_video_application_error(eSetFrameRateFailure);
                    break;
            } // end switch (msgType)
        	break ;
        case (ERROR_INVALID_CONTENT):
            switch (msgType)
            {
                case (CAMERA_SETTINGS):
                    // Unable to set Camera Settings, message received contained invalid content
                    send_video_application_error(eCameraUnableToSendData);
                    send_video_application_error(eCameraSettingsFailure);
                    break;
                case (CAMERA_RESOLUTION):
                    // Unable to set Camera Resolution, message received contained invalid content
                    send_video_application_error(eCameraUnableToSendData);
                    send_video_application_error(eSetResolutionFailure);
                    break;
                case (CAMERA_COMPRESSION):
                    // Unable to set Camera Compression, message received contained invalid content
                    send_video_application_error(eCameraUnableToSendData);
                    send_video_application_error(eSetCompressionFailure);
                    break;
                case (CAMERA_FRAME_RATE):
                    // Unable to set Camera Frame Rate, message received contained invalid content
                    send_video_application_error(eCameraUnableToSendData);
                    send_video_application_error(eSetFrameRateFailure);
                    break;
            } // end switch (msgType)
        	break ;
        case (ERROR_NULL_CONTENT):
            switch (msgType)
            {
                case (CAMERA_SETTINGS):
                    // Unable to set Camera Settings, message received contained invalid content
                    send_video_application_error(eCameraUnableToSendData);
                    send_video_application_error(eCameraSettingsFailure);
                    break;
                case (CAMERA_RESOLUTION):
                    // Unable to set Camera Resolution, message received contained invalid content
                    send_video_application_error(eCameraUnableToSendData);
                    send_video_application_error(eSetResolutionFailure);
                    break;
                case (CAMERA_COMPRESSION):
                    // Unable to set Camera Compression, message received contained invalid content
                    send_video_application_error(eCameraUnableToSendData);
                    send_video_application_error(eSetCompressionFailure);
                    break;
                case (CAMERA_FRAME_RATE):
                    // Unable to set Camera Frame Rate, message received contained invalid content
                    send_video_application_error(eCameraUnableToSendData);
                    send_video_application_error(eSetFrameRateFailure);
                    break;
            } // end switch (msgType)
        	break ;
        case (ERROR_PARSE_FAILURE):
        {
            switch (msgType)
            {
                case (CAMERA_SETTINGS):
                    // Unable to parse the data received from IPC process
                    send_video_application_error(eCameraUnableToSendData);
                    send_video_application_error(eCameraSettingsFailure);
                    break;
                case (CAMERA_RESOLUTION):
                    // Unable to parse the data received from IPC process
                    send_video_application_error(eCameraUnableToSendData);
                    send_video_application_error(eSetResolutionFailure);
                    break;
                case (CAMERA_COMPRESSION):
                    // Unable to parse the data received from IPC process
                    send_video_application_error(eCameraUnableToSendData);
                    send_video_application_error(eSetCompressionFailure);
                    break;
                case (CAMERA_FRAME_RATE):
                    // Unable to parse the data received from IPC process
                    send_video_application_error(eCameraUnableToSendData);
                    send_video_application_error(eSetFrameRateFailure);
                    break;
            } // end switch (msgType)
        	break ;
        case (ERROR_HANDLE_FAILURE):
            switch (msgType)
            {
                case (CAMERA_SETTINGS):
                    // Unable to set Camera Settings, internal error
                    send_video_application_error(eCameraSettingsFailure);
                    break;
                case (CAMERA_RESOLUTION):
                    // Unable to set Camera Resolution, internal error
                    send_video_application_error(eSetResolutionFailure);
                    break;
                case (CAMERA_COMPRESSION):
                    // Unable to set Camera Compression, internal error
                    send_video_application_error(eSetCompressionFailure);
                    break;
                case (CAMERA_FRAME_RATE):
                    // Unable to set Camera Frame Rate, internal error
                    send_video_application_error(eSetFrameRateFailure);
                    break;
            } // end switch (msgType)
        	break ;
        case (ERROR_HTTP_RESPONSE):
            switch (msgType)
            {
                case (CAMERA_SETTINGS):
                    // Unable to set Camera Settings, camera error
                    send_video_application_error(eCameraSetupFailure);
                    send_video_application_error(eCameraSettingsFailure);
                    break;
                case (CAMERA_RESOLUTION):
                    // Unable to set Camera Resolution, camera error
                    send_video_application_error(eCameraSetupFailure);
                    send_video_application_error(eSetResolutionFailure);
                    break;
                case (CAMERA_COMPRESSION):
                    // Unable to set Camera Compression, camera error
                    send_video_application_error(eCameraSetupFailure);
                    send_video_application_error(eSetCompressionFailure);
                    break;
                case (CAMERA_FRAME_RATE):
                    // Unable to set Camera Frame Rate, camera error
                    send_video_application_error(eCameraSetupFailure);
                    send_video_application_error(eSetFrameRateFailure);
                    break;
            } // end switch (msgType)
        	break ;
        case (ERROR_AUTHENTICATION_FAILURE):
            switch (msgType)
            {
                case (CAMERA_SETTINGS):
                    // Camera credentials are invalid.  Unable to send commands.
                    send_video_application_error(eCameraInvalidCredentials);
                    send_video_application_error(eCameraSettingsFailure);
                    break;
                case (CAMERA_RESOLUTION):
                    // Camera credentials are invalid.  Unable to send commands.
                    send_video_application_error(eCameraInvalidCredentials);
                    send_video_application_error(eSetResolutionFailure);
                    break;
                case (CAMERA_COMPRESSION):
                    // Camera credentials are invalid.  Unable to send commands.
                    send_video_application_error(eCameraInvalidCredentials);
                    send_video_application_error(eSetCompressionFailure);
                    break;
                case (CAMERA_FRAME_RATE):
                    // Camera credentials are invalid.  Unable to send commands.
                    send_video_application_error(eCameraInvalidCredentials);
                    send_video_application_error(eSetFrameRateFailure);
                    break;
            } // end switch (msgType)
    		break ;
        } // end switch (errorNumber)
    } //processConfigError
}

/**
 * 10Hz timer callback function, which will get current menu selection.
 *
 * @param[in] tid    Not used
 * @param[in] xcount Not used
 * @param[in] p_user Not used
 *
 * @return    None
 */
static void _10Hz_timer_callback(timer_id_t tid, INT32 xcount, void *p_user)
{
    g_mode = get_responder_mode();

    if (g_mode == HB_QUIT)
    {
        ptc_stop();
    }
} //_10Hz_timer_callback

/*
 * returns 0 if the specified process ID exists and the process is running.
 * otherwise 1 is returned
 * Note that 'stopped' and 'continued' status changes are not considered fatal
 */
static int check_worker_status(int pid)
{
    int status = 0;
    int rc     = 0;

    rc = waitpid(pid, &status, WNOHANG);

    if ( rc > 0 ) {
        if (WIFEXITED(status)) {
            syslog(LOG_ALERT, "check_worker_status: Processs %d unexpectedly exited , status=%d\n", pid, WEXITSTATUS(status));
            rc = 1 ;
        }
        else if (WIFSIGNALED(status)) {
            syslog(LOG_ALERT, "check_worker_status: Processs %d killed by signal\n", pid);
            rc = 1 ;
        }
        else if (WIFSTOPPED(status)) {
            syslog(LOG_ALERT, "check_worker_status: Processs %d stopped\n", pid);
            rc = 1 ;
        }
        else if (WIFCONTINUED(status)) {
            syslog(LOG_ALERT, "check_worker_status: Processs %d continued\n", pid);
            rc = 0 ;
        }
        else {
        	rc = 0 ;
        }

    }
    else if (rc < 0) {
        syslog(LOG_ALERT, "check_worker_status: No such process: %d\n", pid);
    	rc = 1 ;
    }
    else {
    	rc = 0 ;
    }

    return (rc);
}

/*
 * keeps track of camera is_online runtime flag.
 * Sends the CameraStatus PTC message if camera online status has changed
 */
static int reflect_camera_online_status(media_info_t * p_minfo, camdef_t *p_camdef, int id, int is_alive)
{
    char tmp[CAMERA_NAME_LENGTH + 1] = {0};
    static int camon_ttl[MAX_CAMS] = {CAMON_TTL, CAMON_TTL, CAMON_TTL, CAMON_TTL, CAMON_TTL, CAMON_TTL, CAMON_TTL, CAMON_TTL, CAMON_TTL, CAMON_TTL, CAMON_TTL, CAMON_TTL, CAMON_TTL, CAMON_TTL, CAMON_TTL, CAMON_TTL};
    int rc = 0;
    int restart_flag = 1;

    if (is_alive)
    {
        camon_ttl[id] = CAMON_TTL;
    }

	if (p_camdef->runtime.is_online != is_alive)
	{
		if (is_alive == 0) {	/* i.e. about to change to offline */

			/* ignore if still have more lives */
			if (camon_ttl[id] > 1) {
				camon_ttl[id] -= 1 ;
				return 1 ;
			}
			//'is_alive' is already being checked. Hence, removing the redundant check.
            syslog(LOG_INFO, "%s: camera[%d] '%s' changed status to offline.\n", __func__, id, p_camdef->name);
            /*
			 * see if the camera is recording. Ignore the '!is_alive' status, if so.
			 * The recording process will take care of everything ..
			 */
			if ((p_camdef->runtime.recording_status_flags &
				(IS_RECORDING_CHM_AUDIO | IS_RECORDING_SSD_AUDIO | IS_RECORDING_CHM_VIDEO | IS_RECORDING_SSD_VIDEO)) != 0) {

		        memset(tmp, 0, sizeof(tmp));
		        strncpy(tmp, p_camdef->name, CAMERA_NAME_LENGTH);
		        trunc_name(&tmp[0], CAMERA_NAME_LENGTH) ;

		        if (p_camdef->worker_pid > 0) {
					if (check_worker_status(p_camdef->worker_pid) > 0) {
						syslog(LOG_ALERT, "%s: Camera '%s': Recording Process %d died\n", __func__, &tmp[0], p_camdef->worker_pid);
						waitpid(p_camdef->worker_pid, NULL, 0) ;	/* cleanup defunct PID */
						p_camdef->worker_pid = 0 ;
	                    // Clean up.
						camera_recording_just_stopped(p_minfo, p_camdef) ;
						rc = is_alive ;
					}
					else {
						/*It is rear case. if camera is offline and worker is running let terminate it*/
						stop_recording_process(p_minfo,p_camdef);
					}
		        }
		        else {
	                p_camdef->runtime.recording_status_flags &= (~(DO_RECORD_CHM_VIDEO | DO_RECORD_CHM_AUDIO)) ;
	                p_camdef->runtime.recording_status_flags &= (~(DO_RECORD_SSD_VIDEO | DO_RECORD_SSD_AUDIO)) ;

	            	if (p_camdef->we.lwe_wordv) {
	            		l_wordfree(&(p_camdef->we)) ;
	            		memset(&(p_camdef->we), 0, sizeof(p_camdef->we)) ;
	            	}
	    		}
			}
		} // end if (is_alive == 0)
		else if (p_camdef->runtime.is_assigned)
		{
			if (p_camdef->runtime.recording_mode_flags != 0 )
            {
                //'is_alive' is already being checked. Hence, removing the redundant check.
                syslog(LOG_INFO, "%s: camera '%s' changed status to online.\n", __func__, p_camdef->name);

                if (p_camdef->worker_pid > 0) {
						if (check_worker_status(p_camdef->worker_pid) <= 0) {
							restart_flag = 0;
							camon_ttl[id] = 10*CAMON_TTL ;
					}
				}
				else if (--camon_ttl[id] >= 0)
				{
					restart_flag = 0;
					camon_ttl[id] = 10*CAMON_TTL;
				}

				if (restart_flag)
				{
					syslog(LOG_NOTICE,"%s: restarting recording for camera %s",__func__,p_camdef->name);
					/* Need to start recording */
					start_recording_process(p_minfo,p_camdef);
				}
			}
		}

		if (p_camdef->runtime.is_online != is_alive) {
			p_camdef->runtime.is_online = is_alive ;
			send_camera_status(p_camdef);
			rc = is_alive ;
			camon_ttl[id] = CAMON_TTL ;
		}
	}

	return (rc);
}

/**
 * 1Hz timer callback function called periodically in order to convey
 * possible changes in runtime configuration to the HTTP server.
 *
 * @param[in] tid    Not used
 * @param[in] xcount Not used
 * @param[in] p_user Not used
 *
 * @return    None
 */
static void _1Hz_timer_callback(timer_id_t tid, INT32 xcount, void *p_user)
{
	struct media_runtime_s m_status;
	struct cam_runtime_s   c_status[MAX_CAMS];
	char   names[16][16] = {{0}};

	media_info_t * p_minfo = raconf_get_media_info(0) ;

	struct attrlist_s {
		char *attr_name ;
		size_t attr_offset ;
		int is_string ;
	};

	static const struct attrlist_s media_attr[] = {
			{ "is_chm_ready", offsetof(struct media_runtime_s, is_chm_ready), 0 },
			{ "is_rssd_ready", offsetof(struct media_runtime_s, is_rssd_ready), 0 },
			{ "n_cams_recording", offsetof(struct media_runtime_s, n_cams_recording), 0 },
			{ "loco_id", offsetof(struct media_runtime_s, loco_id), 1 },            
			{ NULL, 0, 0 }
	};

	static const struct attrlist_s camera_attr[] = {
			{ "is_assigned", offsetof(struct cam_runtime_s, is_assigned), 0 },
			{ "is_online", offsetof(struct cam_runtime_s, is_online), 0 },
			{ "resolution", offsetof(struct cam_runtime_s, resolution), 0 },
			{ "compression", offsetof(struct cam_runtime_s, compression), 0 },
			{ "frame_rate", offsetof(struct cam_runtime_s, frame_rate), 0 },
			{ "recording_mode_flags", offsetof(struct cam_runtime_s, recording_mode_flags), 0 },
			{ "recording_status_flags", offsetof(struct cam_runtime_s, recording_status_flags), 0 },
            { "commanded_paused", offsetof(struct cam_runtime_s, commanded_paused), 0 },
			{ NULL, 0, 0 }
	};

    int i = 0, n = 0, len = 0, camera_found = 0;
    struct attrlist_s *p_attr;
    char string[256] = {0};
    char *p = NULL;

    static int pass_flag = 0;
    static camon_status_t camon_status[MAX_CAMS];
    static int camon_ncams = -1; /* number of active cameras reported during a previous camon "_rx" pass. -1 -- not available */
    char ip_address[IP_ADD_LENGTH] = {0}, cam_name[IP_ADD_LENGTH] = {0};

	if (p_minfo != NULL) 
    {
		camdef_t *p_camdef ;
		n = p_minfo->n_cameras ;

        /*if (p_minfo->camon_sock > 0)
        {*/
        if (pass_flag == 0)
        {
            int j = 0;
            camon_perform_tx(p_minfo->camon_sock);
            pass_flag = 1;

				for (i=0; i < n; i++) {
					p_camdef = &p_minfo->cameras[i] ;
					if (p_camdef->runtime.is_assigned) {
						/* find the assigned camera among those which reported */
                        camera_found = 0;
						for (j=0; j < camon_ncams; j++) {
#ifndef QNX_BUILD
							if (strstr(p_camdef->homepage, &camon_status[j].ip_addr[0]) != NULL) {
								if (strncasecmp(p_camdef->disp_mac, &camon_status[j].mac_addr[0], CAMERA_NAME_LENGTH) == 0) {
									camon_status[j].is_alive = reflect_camera_online_status(p_minfo, p_camdef, i, camon_status[j].is_alive) ;
#else
                                camon_status[j].is_alive = reflect_camera_online_status(p_minfo, p_camdef, i, 1);
#endif
                                    camera_found = 1;
									break ;
								}
							}
						} // end for (j = 0; j < camon_ncams; j++)

						/* camera was not reported by camon -- it is offline */
						if (p_camdef->runtime.is_assigned && camera_found != 1) {
                            camon_status[j].is_alive = reflect_camera_online_status(p_minfo, p_camdef, i, 0) ;
                            if (camon_status[j].is_alive == 0)
                            {
                                startTimer++;
                                if (startTimer >= START_TIMER_LIMIT)
                                {
                                    startTimer = START_TIMER_LIMIT;

                                    if (get_ip_address_from_mac(p_camdef->disp_mac, ip_address) == 1) 
                                    {
                                        startTimer = 0;
                                        clearArp = 0;
										
                                        replace_ip(p_camdef->homepage,ip_address);
                                        replace_ip(p_camdef->stream_url,ip_address);
                                        replace_ip(p_camdef->snapshot_url,ip_address);
                                        
                                        // remove trailing '_'s from camera name
                                        memset(cam_name, 0, sizeof(cam_name)) ;
                                        strncpy(cam_name, p_camdef->name, CAMERA_NAME_LENGTH) ;
                                        trunc_name(cam_name, CAMERA_NAME_LENGTH) ;
                                        
                                        mxml_node_t *node = NULL;
                                        const char *cur_value ;
                                        // Update Base config file.
                                        node = get_configation_file((char *) REAGENT_CURRENT_CONFIG_FILE);

                                        if (node != NULL)
                                        {
                                            update_configuration_file((mxml_node_t *) node, (char *) cam_name, 
                                            (char *) p_camdef->homepage,
                                            (char *) p_camdef->snapshot_url,
                                            (char *) p_camdef->stream_url ) ;

                                            if ((save_configuration_file((char *) REAGENT_CURRENT_CONFIG_FILE, (mxml_node_t *) node)) == -1)
                                            {
                                                syslog(LOG_NOTICE, "%s , %d,Error Saving Config file", __func__, __LINE__);
                                            }
                                            
                                            node = NULL;
                                        }                  
                                    }
                                }
                                    
                            }
                        }
					} // end if (p_camdef->runtime.is_assigned)
				} // end for (i = 0; i < n; i++)
				camon_ncams = -1 ;	/* until the next "_rx" pass */
			}
			else {
#ifndef QNX_BUILD
				camon_ncams = camon_perform_rx(p_minfo->camon_sock, &camon_status[0], MAX_CAMS, p_minfo, n) ;
#else
				camon_ncams = n ;
#endif
            pass_flag = 0;
        }


		for (i=0; i < n; i++) {
			p_camdef = &p_minfo->cameras[i] ;

			memset(&names[i][0], 0, sizeof(names[0])) ;
			strncpy(&names[i][0], p_camdef->name, CAMERA_NAME_LENGTH) ;
			trunc_name(&names[i][0], CAMERA_NAME_LENGTH) ;
			memcpy((char *) &c_status[i], (char *) &(p_camdef->runtime), sizeof(c_status[0])) ;

			/* check if camera suppose to record? */
			if 	(p_camdef->runtime.recording_mode_flags != 0){

				

				if (p_camdef->worker_pid != 0) {
					rp_ttl[i] = 0;
					if (check_worker_status(p_camdef->worker_pid) > 0) {
						syslog(LOG_ALERT, "%s: Camera '%s': Recording Process %d died", __func__, &names[i][0], p_camdef->worker_pid);
						waitpid(p_camdef->worker_pid, NULL, 0) ;	/* cleanup defunct PID */
						p_camdef->worker_pid = 0 ;
						p_camdef->runtime.is_online = 0;
	                    // Clean up
						camera_recording_just_stopped(p_minfo, p_camdef) ;	
					}
				} // end if (p_camdef->worker_pid != 0)
				else if ((p_camdef->runtime.is_online) && (p_camdef->runtime.is_assigned))  // if camera is commanded to record
				{
					if (rp_ttl[i]++ > 2*CAMON_TTL)
					{
						rp_ttl[i] = 0;

						/* 	we will not start process right away. 
						 	we will wait for 6 cycles and then start recording.
						 	it is possible camera went offline, so let's wait first.
						 */
						syslog(LOG_NOTICE,"%s[%d]: Restarting camera %s[%d] recordings %s ",__func__,__LINE__,names[i],i,p_camdef->name);
						start_recording_process(p_minfo,p_camdef);
						
					}
				} // end else if ((p_camdef->runtime.is_online) && (p_camdef->runtime.is_assigned)) 
			} // end if (p_camdef->runtime.recording_mode_flags != 0)
		} // end for (i = 0; i < n; i++)

		// check metarec 
		handle_metarec(p_minfo);
		// check space_monitor
		handle_space_monitor(p_minfo);

		if (g_extra_lstate != NULL) {

			memcpy((char *) &m_status, (char *) &(p_minfo->runtime), sizeof(m_status)) ;

            /*** Media runtime Status ***/
            memset(&string[0], 0, sizeof(string));

			len += snprintf(&string[len], (sizeof(string)-len-1) , "%s", "shared.sMedia='local mstatus={ ") ;
			p_attr = (struct attrlist_s *) &media_attr[0] ;
			while (p_attr->attr_name) {
				len += snprintf(&string[len], (sizeof(string)-len-1) , "%s%s=",
					(p_attr == &media_attr[0]) ? "": ", ", p_attr->attr_name) ;

				p = ((char *) &m_status) + p_attr->attr_offset ;
				if (p_attr->is_string) {
					len += snprintf(&string[len], (sizeof(string)-len-1) ,"\"%s\"", p) ;
				}
				else {
					len += snprintf(&string[len], (sizeof(string)-len-1) ,"%d", *((int *)p)) ;
				}
				++p_attr ;
			}

			len += snprintf(&string[len], (sizeof(string)-len-1) ,"%s", " }; return mstatus ;'");
			// syslog(LOG_INFO, "%s:%s:%d: string %s", __FILE__, __func__, __LINE__, string);
			luaL_dostring(g_extra_lstate, string);

			/*** Camera runtime Status ***/
			for (i=0; i < n; i++) {

				len = 0 ;
				memset(&string[0], 0, sizeof(string)) ;

				if (i == 0) {
					len += snprintf(&string[len], (sizeof(string)-len-1) , "shared.n_cameras=%d;", p_minfo->n_cameras) ;
				}

				len += snprintf(&string[len], (sizeof(string)-len-1) , "shared.sCam%d=%s", i+1, "'local cstatus={ ") ;
				p_attr = (struct attrlist_s *) &camera_attr[0] ;
				while (p_attr->attr_name) {
					len += snprintf(&string[len], (sizeof(string)-len-1) , "%s%s=",
						(p_attr == &camera_attr[0]) ? "": ", ", p_attr->attr_name) ;

					p = ((char *) &c_status[0]) + p_attr->attr_offset + i * sizeof(struct cam_runtime_s) ;
					if (p_attr->is_string) {
						len += snprintf(&string[len], (sizeof(string)-len-1) ,"'%s'", p) ;
					}
					else {
						len += snprintf(&string[len], (sizeof(string)-len-1) ,"%d", *((int *)p)) ;
					}
					++p_attr ;
				}

				len += snprintf(&string[len], (sizeof(string)-len-1) ," }; return cstatus,\"%s\" ;'", &names[i][0]) ;
				luaL_dostring(g_extra_lstate, string);

			} // end for (i = 0; i < n; i++) /*** Camera runtime Status ***/
		} // end if (g_extra_lstate != NULL)
	} // end if (p_minfo != NULL) 
} //_1Hz_timer_callback

// TODO remove this when the old video_rec gets retired
/**
 * Function to send the Command Ack to video_rec
 *
 * @param[in] msgId        - ID of the message being acked
 * @param[in] optionalData - Optional Information
 *
 * @return    None
 */
void sendCommandAck(uint32_t msgId, uint64_t optionalData)
{
/*
 * RA will not be sending any Acknowledgements
 */
	va_command_ack_t commandAckMsg;

    memset(&commandAckMsg, 0, sizeof(va_command_ack_t));

    // Set Command Ack Message variables
    commandAckMsg.payload.header.version = COMMAND_ACK_MSG_VERSION;
    commandAckMsg.payload.rx_msg_id      = msgId;
    commandAckMsg.payload.optional_data  = optionalData;

    // Send Command Ack
    PTC_SEND_MSG_WITH_PAYLOAD(COMMAND_ACK, commandAckMsg, va_command_ack_t);

} //sendCommandAck

/**
 * Process Time Info Message from video_rec
 * and create new files/folders accordingly
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_time_info(void *p_user, const ptc_msg_info_t *p_info)
{
    time_info_t *p_data = (time_info_t *) p_info->p_data;
#ifdef DEBUG
    LOGF(LSYS, "va_time_info_t\n");
#endif
    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_time_info_t);

    /* Add message-specific processing... */
    if (p_data->header.version != TIME_INFO_MSG_VERSION)
    {
        LOGF(LERR, "Unexpected time_info_t version %d: should be %d\n", p_data->header.version, TIME_INFO_MSG_VERSION);
    }
    else
    {
		static char ti_buf[128] = { 0 };
		char        srcbuf[16];
		int         rc          = 0;
		extern int  errno;

		memset(srcbuf, 0, sizeof(srcbuf)) ;
		strncpy(srcbuf, p_data->time_source, TIME_SOURCE_LENGTH) ;

		sprintf(ti_buf, "timestamp=%u, time_offset=%lld, src='%s'",
			    p_data->system_time, (long long) (p_data->time_offset), srcbuf);

		rc = setenv("TIME_INFO", ti_buf, 1);
#ifdef DEBUG
        show_it(p_info);
#endif
	}
} //msgcb_time_info

/**
 * Process Loco ID Message from video_rec
 * and create new files/folders accordingly
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_locomotive_id(void *p_user, const ptc_msg_info_t *p_info)
{
	locomotive_id_t *p_data;
    media_info_t    *tmp_info; 
    locomotive_id_t  locomotiveId;
    int              locoIdSize    = 0;
    
    if ((p_info == NULL) || (p_info->p_data == NULL))
	{
		syslog(LOG_INFO, "%s: Bad parameter. Return", __func__);		
		return;
	}

	p_data = (locomotive_id_t *) p_info->p_data;

#ifdef DEBUG
    LOGF(LSYS, "va_locomotive_id_t\n");
#endif
    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_locomotive_id_t);

    /* Add message-specific processing... */
    if (p_data->header.version != LOCOMOTIVE_ID_MSG_VERSION)
    {
        LOGF(LERR, "Unexpected locomotive_id_t version %d: should be %d\n", p_data->header.version, LOCOMOTIVE_ID_MSG_VERSION);
    }
    else
    {
#ifdef DEBUG    	
    	show_it(p_info);
#endif
        tmp_info = raconf_get_media_info(0);     // obtain current configuration

        memset((char *)&locomotiveId, 0, sizeof(locomotiveId));
        memcpy(locomotiveId.locomotive_id, p_data->locomotive_id, sizeof(locomotiveId.locomotive_id));
        locoIdSize = strlen(locomotiveId.locomotive_id);

        if (locoIdSize > MAX_LOCO_ID_SIZE)
        {
            locoIdSize = MAX_LOCO_ID_SIZE;  // if Loco ID size > MAX_LOCO_ID_SIZE, set it to MAX_LOCO_ID_SIZE
        }

        // Update the run-time info
        memset(tmp_info->runtime.loco_id, 0, LOCO_ID_STORAGE_SIZE);
        memset(tmp_info->runtime.loco_id, '_', MAX_LOCO_ID_SIZE);   // Pad Loco ID with '_' in case size less than MAX_LOCO_ID_SIZE
        strncpy(tmp_info->runtime.loco_id, p_data->locomotive_id, locoIdSize);

        // Update config files.
        update_config_files((char *) tmp_info->runtime.loco_id);
#if 0
        if (locoIdSize < MAX_LOCO_ID_SIZE)
        {
            // Pad locomotiveId with '_' on the right, in order to match the format of the current Loco ID stored in the configuration
            memset(locomotiveId.locomotive_id, '_', MAX_LOCO_ID_SIZE - locoIdSize);
            strncpy(tmp_info->runtime.loco_id, p_data->locomotive_id, MAX_LOCO_ID_SIZE);

        }
#endif        
		syslog(LOG_INFO, "%s:%s: Loco ID message received: %s", __FILE__, __func__, tmp_info->runtime.loco_id);
	}
} //msgcb_locomotive_id

/**
 * Process Camera Settings Message from video_rec
 * and create/update camera stream accordingly
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_camera_settings(void *p_user, const ptc_msg_info_t *p_info)
{
	camera_settings_t *p_data   = (camera_settings_t *) p_info->p_data;
    media_info_t      *p_minfo  = raconf_get_media_info(0); // obtain ptr to current configuration
    uint64_t           crt      = 0;
#ifdef DEBUG
    LOGF(LSYS, "va_camera_settings_t\n");
#endif
    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_camera_settings_t);

    /* Add message-specific processing... */
    if (p_data->header.version != CAMERA_SETTINGS_MSG_VERSION)
    {
        LOGF(LERR, "Unexpected camera_settings_t version %d: should be %d\n", p_data->header.version, CAMERA_SETTINGS_MSG_VERSION);
    }
    else if (p_minfo != NULL)
    {
        camdef_t *p_camdef;
        char namebuf[CAMERA_NAME_LENGTH + 1] = {0};
        int is_dirty = 0;

    	memset(namebuf, 0, sizeof(namebuf)) ;
    	strncpy(namebuf, p_data->camera, CAMERA_NAME_LENGTH) ;

    	p_camdef = raconf_find_camera(p_minfo, namebuf) ;

    	if (p_camdef != NULL) {
    		if (p_camdef->runtime.resolution != p_data->resolution) {
    			p_camdef->runtime.resolution = p_data->resolution;
    			is_dirty |= 1 ;
    		}
    		if (p_camdef->runtime.compression != p_data->compression) {
    			p_camdef->runtime.compression = p_data->compression;
    			is_dirty |= 1 ;
    		}
    		if (p_camdef->runtime.frame_rate != p_data->frame_rate) {
    			p_camdef->runtime.frame_rate = p_data->frame_rate;
				is_dirty |= 1 ;
    		}
    		if (p_data->header.version > 2 && p_camdef->runtime.max_age != p_data->grooming_duration) {
    			p_camdef->runtime.max_age = p_data->grooming_duration;
    			crt = ((uint64_t)p_camdef->runtime.max_age) * 60 * 60 * 1000000; // Converting hours to micro seconds
				
				set_track_age(namebuf,p_camdef->disp_mac,crt);
    		}
    		if (p_data->no_audio_recording != ((p_camdef->runtime.recording_mode_flags & (DO_RECORD_CHM_AUDIO | DO_RECORD_SSD_AUDIO)) == 0) ) {
				if (p_data->no_audio_recording) {
					p_camdef->runtime.recording_mode_flags &= (~(DO_RECORD_CHM_AUDIO | DO_RECORD_SSD_AUDIO)) ;
				}
				else {
					p_camdef->runtime.recording_mode_flags |= (DO_RECORD_CHM_AUDIO | DO_RECORD_SSD_AUDIO) ;
				}
				is_dirty |= 1 ;
    		}
    		if (p_data->no_chm_recording != ((p_camdef->runtime.recording_mode_flags & DO_RECORD_CHM_VIDEO ) == 0) ) {
    			if (p_data->no_chm_recording) {
    				p_camdef->runtime.recording_mode_flags &= (~(DO_RECORD_CHM_VIDEO | DO_RECORD_CHM_AUDIO)) ;
    			}
    			else {
    				p_camdef->runtime.recording_mode_flags |= (DO_RECORD_CHM_VIDEO | DO_RECORD_CHM_AUDIO) ;
    			}
    			is_dirty = 1 ;
    		}

    		if (is_dirty) {
    			// TODO changes made to camera parameters on the fly can potentially camera recording to be stopped/resumed
    		}
    	} // end if (p_camdef != NULL)
        else
        {
            // Send Application Error
            send_video_application_error(eCameraNameMismatch);
        }
	} // else if (p_minfo != NULL)
} //msgcb_camera_settings

/**
 * Process Camera Resolution Message from video_rec
 * and update the Resolution of the identified camera
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_camera_resolution(void *p_user, const ptc_msg_info_t *p_info)
{
	camera_resolution_t *p_data        = (camera_resolution_t *) p_info->p_data;
    camera_resolution_t  camResolution; 

    media_info_t *p_minfo = raconf_get_media_info(0);
    camdef_t     *p_camdef ;
    char          namebuf[CAMERA_NAME_LENGTH+1] = {0};

#ifdef DEBUG
    LOGF(LSYS, "va_camera_resolution_t\n");
#endif
    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_camera_resolution_t);

    /* Add message-specific processing... */
    if (p_data->header.version != CAMERA_RESOLUTION_MSG_VERSION)
    {
        LOGF(LERR, "Unexpected camera_resolution_t version %d: should be %d\n", p_data->header.version, CAMERA_RESOLUTION_MSG_VERSION);
    }
    else
    {
#ifdef DEBUG
        show_it(p_info);
#endif
        memset((char*) &camResolution, 0, sizeof(camResolution));
        strncpy(camResolution.camera, p_data->camera, CAMERA_NAME_LENGTH);
        camResolution.resolution = p_data->resolution;

    	memset(namebuf, 0, sizeof(namebuf)) ;
    	strncpy(namebuf, p_data->camera, CAMERA_NAME_LENGTH) ;

    	p_camdef = raconf_find_camera(p_minfo, namebuf) ;
    	if (p_camdef == NULL)
    		return;

    	/*Updating runtime value on reset new value will be configured on camera
    	stream will be stopped and started again*/
    	p_camdef->runtime.resolution = p_data->resolution;
        

		if (EXIT_FAILURE == reset_camera_stream(&namebuf[0]))
			processConfigError(ERROR_HANDLE_FAILURE, CAMERA_RESOLUTION);


        // TODO
        // Update Video Metadata to denote that Resolution has changed due to a digital event
	}
} //msgcb_camera_resolution

/**
 * Process Camera Compression Message from video_rec
 * and update the Compression of the identified camera
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_camera_compression(void *p_user, const ptc_msg_info_t *p_info)
{
	camera_compression_t *p_data          = (camera_compression_t *) p_info->p_data;
    camera_compression_t  camCompression;

    media_info_t *p_minfo  = raconf_get_media_info(0);
    camdef_t     *p_camdef;
    char          namebuf[CAMERA_NAME_LENGTH+1] = {0};

#ifdef DEBUG
    LOGF(LSYS, "va_camera_compression_t\n");
#endif
    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_camera_compression_t);

    /* Add message-specific processing... */
    if (p_data->header.version != CAMERA_COMPRESSION_MSG_VERSION)
    {
        LOGF(LERR, "Unexpected camera_compression_t version %d: should be %d\n", p_data->header.version, CAMERA_COMPRESSION_MSG_VERSION);
    }
    else
    {
#ifdef DEBUG    	
        show_it(p_info);
#endif
        memset((char*) &camCompression, 0, sizeof(camCompression));
        strncpy(camCompression.camera, p_data->camera, CAMERA_NAME_LENGTH);
        camCompression.compression = p_data->compression;

    	memset(namebuf, 0, sizeof(namebuf)) ;
    	strncpy(namebuf, p_data->camera, CAMERA_NAME_LENGTH) ;

    	p_camdef = raconf_find_camera(p_minfo, namebuf) ;
    	if (p_camdef == NULL)
    		return;

        /*Updating runtime value on reset new value will be configured on camera
    	stream will be stopped and started again*/
    	p_camdef->runtime.compression = p_data->compression;

		if (EXIT_FAILURE == reset_camera_stream(&namebuf[0]))
			processConfigError(ERROR_HANDLE_FAILURE, CAMERA_COMPRESSION);


        // TODO
        // Update Video Metadata to denote that Compression has changed due to a digital event
	}
} //msgcb_camera_compression

/**
 * Process Camera Frame Rate Message from video_rec
 * and update the Frame Rate of the identified camera
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_camera_frame_rate(void *p_user, const ptc_msg_info_t *p_info)
{
	camera_frame_rate_t *p_data  = (camera_frame_rate_t *) p_info->p_data;
    media_info_t        *p_minfo = raconf_get_media_info(0);
    camdef_t            *p_camdef;
    char namebuf[CAMERA_NAME_LENGTH + 1] = {0};

#ifdef DEBUG
    LOGF(LSYS, "va_camera_frame_rate_t\n");
#endif
    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_camera_frame_rate_t);

    /* Add message-specific processing... */
    if (p_data->header.version != CAMERA_FRAME_RATE_MSG_VERSION)
    {
        LOGF(LERR, "Unexpected camera_frame_rate_t version %d: should be %d\n", p_data->header.version, CAMERA_FRAME_RATE_MSG_VERSION);
    }
    else
    {
        show_it(p_info);
    	memset(namebuf, 0, sizeof(namebuf)) ;
    	strncpy(namebuf, p_data->camera, CAMERA_NAME_LENGTH) ;

    	p_camdef = raconf_find_camera(p_minfo, namebuf) ;
    	if (p_camdef == NULL)
    		return;

    	/*Updating runtime value on reset new value will be configured on camera
    	stream will be stopped and started again*/
    	p_camdef->runtime.frame_rate = p_data->frame_rate;
        
		if (EXIT_FAILURE == reset_camera_stream(&namebuf[0]))
			processConfigError(ERROR_HANDLE_FAILURE, CAMERA_FRAME_RATE);
		
        // TODO
        // Update Video Metadata to denote that Frame Rate has changed due to a digital event
	}
} //msgcb_camera_frame_rate

/**
 * Process Camera Record/Pause Message from video_rec
 * and start/stop video recording
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_camera_record_pause(void *p_user, const ptc_msg_info_t *p_info)
{
    int                    rc     = 0;
	camera_record_pause_t *p_data = (camera_record_pause_t *) p_info->p_data;

#ifdef DEBUG
    LOGF(LSYS, "va_camera_record_pause_t\n");
#endif
    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_camera_record_pause_t);

    /* Add message-specific processing... */
    if (p_data->header.version != CAMERA_RECORD_PAUSE_MSG_VERSION)
    {
        LOGF(LERR, "Unexpected camera_record_pause_t version %d: should be %d\n", p_data->header.version, CAMERA_RECORD_PAUSE_MSG_VERSION);
    }
    else
    {
#ifdef DEBUG    	
    	show_it(p_info);
#endif
        // Start/Stop Recording to the specified camera
        rc = handle_camera_record_pause(p_data);

        if (EXIT_SUCCESS == rc)
        {
            // Camera Record/Pause action happened successfully
        	LOGF(LSYS, "CAMERA_RECORD_PAUSE action succeeded\n") ;
        }
        else
        {
        	LOGF(LERR, "CAMERA_RECORD_PAUSE action failed\n") ;
            // Camera Record/Pause action was unsuccessful

            // TODO
            // Log the Error
        }
	}
} //msgcb_camera_record_pause

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
static void msgcb_ready_status(void *p_user, const ptc_msg_info_t *p_info)
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
    	media_info_t *p_media   = raconf_get_media_info(0);
    	camdef_t     *p_camdef;
    	int           i, n      = 0;
    	int           status    = 0;

    	// Update flags to denote which devices are ready (CHM, RSSD or BOTH)
        // NOTE: Cannot begin recording until this flags are not set.
    	if (p_media != NULL) {

    		// Each rtsprec have its own ptc client, they can listen to SSD state change and handle 
    		// accordingly 
            syslog(LOG_NOTICE, "%s: Ready Status message received: CHM %d, SSD %d", __func__, p_data->chm_ready, p_data->removable_ssd_ready);
    		p_media->runtime.is_chm_ready = (p_data->chm_ready) ? 1 : 0 ;
    		p_media->runtime.is_rssd_ready = (p_data->removable_ssd_ready) ? 1 : 0 ;

    		set_drive_availability(e_chm,(uint8_t)p_media->runtime.is_chm_ready);
    		set_drive_availability(e_rssd,(uint8_t)p_media->runtime.is_rssd_ready);

    		p_camdef = &p_media->cameras[0] ;

    		n = p_media->n_cameras ;

    		for (i=0; i < n; i++) {
    			if (p_camdef->runtime.is_online && !(p_camdef->runtime.commanded_paused)) { // only set flags if camera is online and commanded to record (default on startup)) {
    				p_camdef->runtime.recording_mode_flags = 0 ;

    				if (p_media->runtime.is_chm_ready) {
    					p_camdef->runtime.recording_mode_flags |= DO_RECORD_CHM_VIDEO ;
    					if (str2bool(p_camdef->do_audio) != 0) {
    						p_camdef->runtime.recording_mode_flags |= DO_RECORD_CHM_AUDIO ;
    					}
    				} 

					if (p_media->runtime.is_rssd_ready) {
						p_camdef->runtime.recording_mode_flags |= DO_RECORD_SSD_VIDEO ;
						if (str2bool(p_camdef->do_audio) != 0) {
							p_camdef->runtime.recording_mode_flags |= DO_RECORD_SSD_AUDIO ;
						}
					} 
    			} // end if (p_camdef->runtime.is_online)
				
				/* update the recording status flags to keep consistency since it is recording */
				if (p_camdef->worker_pid > 0)
				{
					p_camdef->runtime.recording_status_flags = 0 ;
					if (p_camdef->runtime.recording_mode_flags & DO_RECORD_CHM_VIDEO) {
						p_camdef->runtime.recording_status_flags |= IS_RECORDING_CHM_VIDEO ;
						if (p_camdef->runtime.recording_mode_flags & DO_RECORD_CHM_AUDIO) {
							p_camdef->runtime.recording_status_flags |= IS_RECORDING_CHM_AUDIO ;
						}
					}
					if (p_camdef->runtime.recording_mode_flags & DO_RECORD_SSD_VIDEO) {
						p_camdef->runtime.recording_status_flags |= IS_RECORDING_SSD_VIDEO ;
						if (p_camdef->runtime.recording_mode_flags & DO_RECORD_SSD_AUDIO) {
							p_camdef->runtime.recording_status_flags |= IS_RECORDING_SSD_AUDIO ;
						}
					}
				} // end if (p_camdef->worker_pid > 0)
    			++p_camdef ;
			} // end for (i=0; i < n; i++)
    	} // end if (p_media != NULL)

        // If the RSSD is not available, Video Recording thread is aware of this
		// send the current recording status on CHM and RSSD
		status = see_if_recording(&(p_media->cameras[0]), p_media->n_cameras);
        send_recording_status_msg(status & 1, status & 2);
#ifdef DEBUG        
        show_it(p_info);
#endif
	}
} //msgcb_ready_status

/**
 * Process Download Message from video_rec, perform download
 * and reply with Download ACK and Download Status
 *
 * @param[in] videoApplicationErrorMsg - Video Application Error Message Structure
 *
 * @return    EXIT_SUCCESS - On Success
 *            EXIT_FAILURE - On Failure
 *
 * NOTE: This is one of TWO API messages that does not require a Command ACK
 */
static void msgcb_download(void *p_user, const ptc_msg_info_t *p_info)
{
    uint64_t       file_size        = 0;
    uint8_t        status           = 0;
    va_download_t *downloadMessage;

    //TODO it Recording agent is busy or frequent download request??
    //while(busy)
    //{
    // wait???
    //}

    if (p_info == NULL)
    {
        LOGF(LERR, "Received: va_download_ack_t: Invalid message pointer p_info\n");
        return;     // just don't do anything with this data
    }
    else
    {
        if (p_info->p_data == NULL)
        {
            LOGF(LERR, "Received: va_download_ack_t: Invalid message pointer p_info->p_data.\n");
            return;     // just don't do anything with this data
        }
        else
        {
            PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_download_t);
            downloadMessage = (va_download_t *)(p_info->p_data);

            do_download(downloadMessage, &file_size, &status);

            // Send the Download Ack (contains file size)
            send_download_ack(file_size);                      // send download ACK if download is not possible then file_size=0

            // Send the Download Status Message (with Status of Download)
            send_download_status(status);                      // send download status message
			
			//TODO: Handle Partial Download
        }
    }
} //msgcb_download



/**
 * Heartbeat request callback function, which will respond according to menu-
 * selected heartbeat response mode.
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_heartbeat_request(void *p_user, const ptc_msg_info_t *p_info)
{
    heartbeat_request_t *p_data = (heartbeat_request_t *) p_info->p_data;
#ifdef DEBUG
    show_it(p_info);
#endif
    ////LOGF(LSYS, "va_heartbeat_request_t\n");

    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_heartbeat_request_t);

    /* Add message-specific processing... */
    if (p_data->header.version != HEARTBEAT_REQUEST_MSG_VERSION)
    {
        LOGF(LERR, "Unexpected heartbeat_request_t version %d: should be %d\n", p_data->header.version, HEARTBEAT_REQUEST_MSG_VERSION);
    }
    else
    {
        switch (g_mode)
        {
            case HB_GOOD_RESPONSE:
            {
                send_heartbeat_response_msg(p_data->sequence, HEARTBEAT_NORMAL);
                break;
            }
            case HB_NO_RESPONSE:
            {
                break;
            }
            case HB_INVALID_SEQUENCE:
            {
                send_heartbeat_response_msg(~p_data->sequence, HEARTBEAT_NORMAL);
                break;
            }
            case HB_RESTART:
            {
                send_heartbeat_response_msg(p_data->sequence, HEARTBEAT_RESTART_REQUIRED);
                break;
            }
            default:
            {
                break;
            }
        } // end switch (g_mode)
    }
} //msgcb_heartbeat_request

/**
 * Process Accessory Power Message from video_rec
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_accessory_power(void *p_user, const ptc_msg_info_t *p_info)
{
	accessory_power_t *p_data   = (accessory_power_t *) p_info->p_data;
#ifdef DEBUG
    LOGF(LSYS, "va_accessory_power_t\n");
#endif
    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_accessory_power_t);

    /* Add message-specific processing... */
    if (p_data->header.version != ACCESSORY_POWER_MSG_VERSION)
    {
        LOGF(LERR, "Unexpected accessory_power_t version %d: should be %d\n", p_data->header.version, ACCESSORY_POWER_MSG_VERSION);
    }
    else
    {
#ifdef DEBUG    	
        show_it(p_info);
#endif
        /* Turn ON/OFF Accessory power based on the command received */
        if (p_data->accessory_power_on)
        {
            //Commands to turn on accessory power
            //NOTE: Currently, we do not have any accessory power
            //devices to turn on or off.
        }
        else
		{
            //Commands to turn off accessory power
            //NOTE: Currently, we do not have any accessory power
            //devices to turn on or off.
        }
	}
} //msgcb_accessory_power

/**
 * Process Request Camera Status Message from video_rec
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_request_camera_status(void *p_user, const ptc_msg_info_t *p_info)
{
	request_camera_status_t *p_data = (request_camera_status_t *) p_info->p_data;
#ifdef DEBUG
    LOGF(LSYS, "va_request_camera_status_t\n");
#endif
    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_request_camera_status_t);

    /* Add message-specific processing... */
    if (p_data->header.version != REQUEST_CAMERA_STATUS_MSG_VERSION)
    {
        LOGF(LERR, "Unexpected request_camera_status_t version %d: should be %d\n", p_data->header.version, REQUEST_CAMERA_STATUS_MSG_VERSION);
    }
    else
    {
    	media_info_t * p_media = raconf_get_media_info(0) ;
    	if (p_media != NULL) {
        	camdef_t *p_camdef ;
        	int ncams = p_media->n_cameras ;
        	int i ;

        	for (i=0; i < ncams; i++) {
        		p_camdef = &p_media->cameras[i] ;

        		send_camera_status(p_camdef);
        	}
    	}
#ifdef DEBUG
    	show_it(p_info);
#endif    	
	}
} //msgcb_request_camera_status

/**
 * Process Camera Snapshot Request Message from video_download
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_camera_snapshot(void *p_user, const ptc_msg_info_t *p_info)
{
    uint64_t           file_size = 0;
    uint8_t            status    = 0;
	camera_snapshot_t *p_data    = (camera_snapshot_t *) p_info->p_data;
#ifdef DEBUG
    LOGF(LSYS, "va_camera_snapshot_t\n");
#endif
    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_camera_snapshot_t);

    /* Add message-specific processing... */
    if (p_data->header.version != CAMERA_SNAPSHOT_MSG_VERSION)
    {
        LOGF(LERR, "Unexpected camera_snapshot_t version %d: should be %d\n", p_data->header.version, CAMERA_SNAPSHOT_MSG_VERSION);
    }
    else
    {
#ifdef DEBUG    	
        show_it(p_info);
#endif
        get_snapshot(p_data, &file_size, &status);
        
        // Send the Download Ack (contains file size)
        send_download_ack(file_size);

        // Send the Download Status Message (with Status of Download)
        send_download_status(status);
	}
} //msgcb_camera_snapshot

/**
 * Process Camera Profile Message from video_rec
 *
 * @param[in] p_user         - Not used
 * @param[in] ptc_msg_info_t - Message Structure Info
 * @param[in] p_info         - Incoming message bytes
 *
 * @return    None
 */
static void msgcb_camera_profile(void *p_user, const ptc_msg_info_t *p_info)
{
    char              camera[CAMERA_NAME_LENGTH + 1]; // 12 characters with NUL (0x0) characters padded to fill empty characters
    uint8_t           cameraProfile                   = 0;
	camera_profile_t *p_data                          = (camera_profile_t *) p_info->p_data;
#ifdef DEBUG
    LOGF(LSYS, "va_camera_profile_t\n");
#endif
    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_camera_profile_t);

    /* Add message-specific processing... */
    if (p_data->header.version != CAMERA_PROFILE_MSG_VERSION)
    {
        LOGF(LERR, "Unexpected camera_profile_t version %d: should be %d\n", p_data->header.version, COMMAND_ACK_MSG_VERSION);
    }
    else
    {
#ifdef DEBUG    	
    	show_it(p_info);
#endif
        // Obtain Camera Profile Message variables in order to set the profile
        memset(camera, 0, sizeof(camera));
        strncpy(camera, p_data->camera, CAMERA_NAME_LENGTH);
        cameraProfile = p_data->camera_profile;

        /* TODO: Set the Camera Profile */
        // CALL_TO_EXTERNAL_FUNCTION(camera, cameraProfile);
	}
} //msgcb_camera_profile

/**
 * Process SHUTDOWN Message from video_rec
 * to shutdown recording on either CHM or SSD or shutdown due to power loss.
 *
 * @param[in] p_user         - Not used
 * @param[in] p_info         - Incoming message bytes message structure
 *
 * @return    None
 */
static void msgcb_shutdown_rec(void *p_user, const ptc_msg_info_t *p_info)
{
    shutdown_t   *p_data    = (shutdown_t *) p_info->p_data;
    media_info_t *p_media   = raconf_get_media_info(0);
    camdef_t     *p_camdef;
    int           i = 0, n      = 0;

    syslog(LOG_NOTICE, "%s: Received shutdown message.", __func__);

    /* Verify message contents */
    PTC_VERIFY_MSG_WITH_PAYLOAD(p_info, va_shutdown_t);

    /* Add message-specific processing... */
    if (p_data != NULL) 
    {
        if (p_data->header.version == SHUTDOWN_MSG_VERSION)
        {
            if (p_media != NULL) 
            {
                switch (p_data->function_type)
                {
                    case SHUTDOWN_CHM:
                        p_media->runtime.is_chm_ready = 0;
                        set_drive_availability(e_chm,e_not_available);
                        break;
                    case SHUTDOWN_REMOVABLE_SSD:
                        p_media->runtime.is_rssd_ready = 0;
                        set_drive_availability(e_rssd,e_not_available);
                        break;
                    case SHUTDOWN_ALL:
                        p_media->runtime.is_chm_ready = 0;
                        p_media->runtime.is_rssd_ready = 0;
                        set_drive_availability(e_chm,e_not_available);
                        set_drive_availability(e_rssd,e_not_available);                           
                        shutdownInProcess = TRUE;
                        break;
                    default:
                        syslog(LOG_ERR, "%s: Shutdown received. Invalid function", __func__);
                        return;
                }

                p_camdef = &p_media->cameras[0] ;
                n        = p_media->n_cameras ;

                for (i=0; i < n; i++) 
                {
                    if (p_media->runtime.is_chm_ready == 0) 
                    {
                        p_camdef->runtime.recording_mode_flags &= (~(DO_RECORD_CHM_VIDEO | DO_RECORD_CHM_AUDIO));
                    } 

                    if (p_media->runtime.is_rssd_ready == 0) {
                        p_camdef->runtime.recording_mode_flags &= (~(DO_RECORD_SSD_VIDEO | DO_RECORD_SSD_AUDIO));
                    } 
                    
                    /* update the recording status flags to keep consistency since it is recording */
                    if (p_media->runtime.is_chm_ready == 0) 
                    {
                        p_camdef->runtime.recording_status_flags &= (~( IS_RECORDING_CHM_VIDEO | IS_RECORDING_CHM_AUDIO));
                    } 

                    if (p_media->runtime.is_rssd_ready == 0) {
                        p_camdef->runtime.recording_status_flags &= (~(IS_RECORDING_SSD_VIDEO | IS_RECORDING_SSD_AUDIO));
                    } 
                    ++p_camdef;
                }
            }

        }
        else
        {
            syslog(LOG_ERR, "%s: Unexpected shutdown_t version %d: should be %d", __func__, p_data->header.version, SHUTDOWN_MSG_VERSION);            
        }
    }
    else {
        syslog(LOG_ERR, "%s: Invalid shutdown function paremeters.",__func__);
    }
} //msgcb_shutdown_rec

/**
 * Callback functions based on the PTC message received
 *
 */
static const ptc_msg_callback_entry_t msg_cb_list[] =
{
	{ TIME_INFO					  , msgcb_time_info},
	{ LOCOMOTIVE_ID               , msgcb_locomotive_id},	
	{ CAMERA_SETTINGS             , msgcb_camera_settings},
	{ CAMERA_RESOLUTION           , msgcb_camera_resolution},
	{ CAMERA_COMPRESSION          , msgcb_camera_compression},
	{ CAMERA_FRAME_RATE           , msgcb_camera_frame_rate},
	{ CAMERA_RECORD_PAUSE         , msgcb_camera_record_pause},
	{ READY_STATUS                , msgcb_ready_status},
	{ DOWNLOAD                    , msgcb_download},
	{ HEARTBEAT_REQUEST           , msgcb_heartbeat_request},
	{ ACCESSORY_POWER             , msgcb_accessory_power},
	{ REQUEST_CAMERA_STATUS       , msgcb_request_camera_status},
	{ CAMERA_SNAPSHOT             , msgcb_camera_snapshot},
	{ CAMERA_PROFILE              , msgcb_camera_profile},
    { SHUTDOWN                    , msgcb_shutdown_rec}
};

/**
 * Function to initialize the PTC Client
 * 
 * @param[in]  argc
 * @param[in] *argv
 * 
 * @return     EXIT_SUCCESS      if Successful
 * @return     EXIT_FAILURE      if Unsuccessful
 * @return     EXIT_PTC_INIT_ERR if Unable to initialize PTC Client
 */
int ptc_client_init(int argc, char *argv[])
{
    ////timer_id_t _10Hz_timer;
    char name[] = "reagent_ptc_cli";

    ptc_initialize(argc, argv);

    logger_add_msg_callback(ptc_logmsg_cb, LOGFLG_NO_SEV_CB) ;
    logger_set_flags(LOGFLG_NO_SEV_NAME | LOGFLG_NO_DATETIME | LOGFLG_NO_TIME_MS /* | LOGFLG_NO_NAME */) ;

    if (ptc_connect(0, name) != SUCCESS)
    {
    	LOG(LERR, "Failed to init ptc\n");
        logger_del_msg_callback(ptc_logmsg_cb) ;
        return (EXIT_PTC_INIT_ERR);
    }

    if (PTC_MSG_ATTACH_LIST(msg_cb_list) != SUCCESS)
    {
        logger_del_msg_callback(ptc_logmsg_cb) ;
        return (EXIT_FAILURE);
    }

    if (ptc_timer_create(&_10Hz_timer, _10Hz_timer_callback, NULL) != SUCCESS)
    {
        logger_del_msg_callback(ptc_logmsg_cb) ;
        return(EXIT_FAILURE);
    }

    if (ptc_timer_start(_10Hz_timer, 100, 100) != SUCCESS)
    {
        ptc_timer_destroy(_10Hz_timer);
        logger_del_msg_callback(ptc_logmsg_cb) ;
        return(EXIT_FAILURE);
    }

    /* 'runtime configuration conveyor' 1Hz timer */
    if (ptc_timer_create(&_1Hz_timer, _1Hz_timer_callback, NULL) != SUCCESS)
    {
        logger_del_msg_callback(ptc_logmsg_cb) ;
        return(EXIT_FAILURE);
    }

    if (ptc_timer_start(_1Hz_timer, 1000, 1000) != SUCCESS)
    {
        ptc_timer_destroy(_1Hz_timer);
        logger_del_msg_callback(ptc_logmsg_cb) ;
        return(EXIT_FAILURE);
    }
    return (EXIT_SUCCESS);
} //ptc_client_init

/**
 * Function to Start the PTC Client
 * 
 * @param[in]  void * pointer to Lua Extra State
 * 
 * @return     None
 */
void ptc_client_run(void *xtra_state)
{
	g_extra_lstate = (lua_State *) xtra_state ;
    ptc_run();
}

/**
 * Function to Stop the PTC Client
 * 
 * @param[in]  None
 * 
 * @return     None
 */
void ptc_client_stop()
{
    if (g_extra_lstate != NULL) {
    	luaif_release_extra_lState(g_extra_lstate) ;
    	g_extra_lstate = NULL ;
    }

    ptc_stop() ;
    logger_del_msg_callback(ptc_logmsg_cb) ;
    ptc_timer_destroy(_1Hz_timer);
    ptc_timer_destroy(_10Hz_timer);
}

//////////////// handle_ptc_msg.c ///////////////////

/**
 * Function to spawn sub-brocess
 *
 * @param[in]   argc
 * @param[in] **argv
 *
 * @return     pid if Successful
 * @return     -1 if Unsuccessful
 */
static pid_t spawn_process(int argc, char **argv)
{
#ifndef QNX_BUILD
    int   fork_rc = fork();
    pid_t pid;

    if (fork_rc != 0)
    {
    	if (fork_rc == -1) {
    		perror("fork") ;
    	}
    	// If non-zero, then this process is the parent, and the forkVal is the child pid.
        pid = fork_rc;
        return pid;
    }

    execv(argv[0], argv);
    ////perror("execv error");
    fprintf(stderr, "execv error: %s -- %s\n", argv[0], strerror(errno));

    return (-1) ;
#else
#include <stddef.h>
#include <process.h>

    int rc ;

    rc = spawnv(P_WAIT, argv[0], argv) ;

    if (rc == -1) {
    	perror("spawnv") ;
    }

    return (pid_t) rc ;
#endif
}

/**
 * Function to check the Status of Recording on the
 * CHM and RSSD
 *
 * @param[in] *p_camdef Pointer to structure containing individual
 *                      camera definition parameters
 * @param[in]  ncams    Number of cameras
 *
 * @return     eNoCamerasRecording (0) if no camera is recording,
 * @return     eRecordingToChm     (1) if at least one camera is recording to CHM
 * @return     eRecordingToRssd    (2) if at least one camera is recording to RSSD
 * @return     eRecordingToBoth    (3) if both conditions above are true
 */
static int see_if_recording(camdef_t *p_camdef, int ncams)
{
	int i  = 0;
	int rc = eRecordingToBoth;

	for (i = 0; i < ncams; i++)
    {
		if (p_camdef->is_essential[0] != NULL) //array, not a pointer
		{
			if (p_camdef->runtime.recording_mode_flags & IS_RECORDING_CHM_VIDEO)
			{
				if ( !(p_camdef->runtime.recording_status_flags & IS_RECORDING_CHM_VIDEO) )
		        {
		        	syslog(LOG_ERR,"%s is not recording on CHM.\n",p_camdef->name);
					rc &= ~eRecordingToChm;
				}
			}
		}

		if (p_camdef->runtime.recording_mode_flags & IS_RECORDING_SSD_VIDEO)
		{
			if ( !(p_camdef->runtime.recording_status_flags & IS_RECORDING_SSD_VIDEO) )
	        {
                syslog(LOG_ERR, "%s is not recording on RSSD.\n", p_camdef->name);
                rc &= ~eRecordingToRssd;
			}
		}
		++p_camdef;
	}
	return(rc);
} //see_if_recording

/*
 * reflects runtime status flags when the camera just went from 'recording' to 'not recording' state
 * properly handles the metadata recording process(es) and tracks
 * the number of currently recording cameras
 */
static void camera_recording_just_stopped(media_info_t *p_minfo, camdef_t *p_camdef)
{
    char tmp[CAMERA_NAME_LENGTH + 1] = {0};
    p_camdef->worker_pid = 0;
    int status = -1;

    memset(tmp, 0, sizeof(tmp));
    strncpy(tmp, p_camdef->name, CAMERA_NAME_LENGTH);
    trunc_name(&tmp[0], CAMERA_NAME_LENGTH) ;

    p_camdef->runtime.recording_status_flags &= (~(DO_RECORD_CHM_VIDEO | DO_RECORD_CHM_AUDIO)) ;
    p_camdef->runtime.recording_status_flags &= (~(DO_RECORD_SSD_VIDEO | DO_RECORD_SSD_AUDIO)) ;
 
 	send_camera_status(p_camdef);
    status = see_if_recording(&(p_minfo->cameras[0]), p_minfo->n_cameras);
	send_recording_status_msg(status & 1, status & 2);

    if (p_minfo->runtime.n_cams_recording > 0) {
    	p_minfo->runtime.n_cams_recording -= 1 ;
    }

    LOGF(LSYS, "Stopped recording from camera '%s', %d camera(s) still recording\n",
    		tmp, p_minfo->runtime.n_cams_recording);

    handle_metarec(p_minfo);
    handle_space_monitor(p_minfo);
}

/**
 * handle_space_monitor
 *
 * @param[in] *p_minfo Pointer to structure 
 * @return     None
 */
static void handle_space_monitor(media_info_t *p_minfo)
{
	space_mon_t *p_space_mon = &(p_minfo->spacemonitor);
	int status = -1;

	if (p_minfo->runtime.n_cams_recording > 0)
	{
		if (p_space_mon->worker_pid <= 0)
		{
			pid_t pid = spawn_process(p_space_mon->we.lwe_wordc, p_space_mon->we.lwe_wordv);

			if (pid != -1){
				p_space_mon->worker_pid = pid;
                syslog(LOG_INFO, "%s: Successfully started space_monitor %d", __func__, p_space_mon->worker_pid);
			}
			else {
				p_space_mon->worker_pid = 0 ;
                syslog(LOG_INFO, "%s: Cannot start space monitor", __func__);
			}
		}
		else
		{
			if (check_worker_status(p_space_mon->worker_pid ) > 0) {
				syslog(LOG_ALERT, "space_monitor Process %d died",p_space_mon->worker_pid);
				waitpid(p_space_mon->worker_pid, NULL, 0) ;	/* cleanup defunct PID */
				p_space_mon->worker_pid = 0 ;
				handle_space_monitor(p_minfo);
			}
		}
	} // end if (p_minfo->runtime.n_cams_recording > 0)
	else
	{
		if (p_space_mon->worker_pid && 0) /*?? space_monitor shouldn't not be killed shared memory is attached to it*/
		{
            syslog(LOG_INFO, "%s: No camera is recording. Stop space monitor pid %d", __func__, p_space_mon->worker_pid);
			kill(p_space_mon->worker_pid, SIGQUIT);
			waitpid(p_space_mon->worker_pid, &status, 0) ;
			p_space_mon->worker_pid = 0;
			LOGF(LSYS, "Stopped space_monitor\n") ;
		}
	}
} // handle_space_monitor

/**
 * stop_space_monitor
 *
 * @param[in] *p_minfo Pointer to structure
 * @return     None
 */
static void stop_space_monitor(media_info_t *p_minfo)
{
        space_mon_t *p_space_mon = &(p_minfo->spacemonitor);
        int status = -1;
        if (p_space_mon->worker_pid )
        {
            kill(p_space_mon->worker_pid , SIGKILL);
            syslog(LOG_INFO, "%s: Process %d waitpid() returned status %d\n", __func__, p_space_mon->worker_pid, WEXITSTATUS(status));
	        waitpid(p_space_mon->worker_pid, &status, 0) ;
	        p_space_mon->worker_pid = 0;
        }
        else
        {
                syslog(LOG_INFO, "%s: No space_monitor process to kill\n", __func__);
        }

} // stop_space_monitor


/**
 * handle_metarec
 *
 * @param[in] *p_minfo Pointer to structure 
 * @return     None
 */
static void handle_metarec(media_info_t *p_minfo)
{
	int              status    = -1;
	meta_recorder_t *pmeta_rec = &(p_minfo->metaworker) ;

	if (p_minfo->runtime.is_chm_ready || p_minfo->runtime.is_rssd_ready)
	{
		if (pmeta_rec->worker_pid <= 0)
		{
			raconf_prepare_for_metarecording(p_minfo);						
			pid_t pid = spawn_process(pmeta_rec->we.lwe_wordc, pmeta_rec->we.lwe_wordv);

			if (pid != -1){
				pmeta_rec->worker_pid = pid;
				syslog(LOG_INFO, "%s: Successfully started metadata recording pid %d", __func__, pmeta_rec->worker_pid); 
			}
			else {
				pmeta_rec->worker_pid = 0 ;
				syslog(LOG_ERR, "%s: Cannot start metadata recording", __func__); 
			}
		}
		else
		{
			if (check_worker_status(pmeta_rec->worker_pid ) > 0) {
				syslog(LOG_ALERT, "Metarec Process %d died",pmeta_rec->worker_pid);
				waitpid(pmeta_rec->worker_pid, NULL, 0) ;	/* cleanup defunct PID */
				pmeta_rec->worker_pid = 0 ;
				handle_metarec(p_minfo);
			}
		}
	}
} //handle_metarec

/**
 * Function to reset (stop and then start) camera recording 
 *
 * @param[in] *camera_name Pointer to Camera Name
 *
 * @return     EXIT_SUCCESS - On Success
 *             EXIT_FAILURE - On Failure
 */
static int reset_camera_stream(char *camera_name)
{
	camera_record_pause_t p_data;
	
	memset(p_data.camera, 0, sizeof(p_data.camera));
    memcpy(p_data.camera, camera_name, CAMERA_NAME_LENGTH);
	
	p_data.record = 0;
	if (EXIT_FAILURE == handle_camera_record_pause(&p_data))
		return EXIT_FAILURE;

	usleep(CAMERA_RESET_SLEEP); // 500msec nap after stopping camera recording

	p_data.record = 1;
	if (EXIT_FAILURE == handle_camera_record_pause(&p_data))
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

/**
 * Function to Start, Pause or Resume Camera Recording
 *
 * @param[in] *p_data Pointer to Camera Record Pause message structure
 *
 * @return     EXIT_SUCCESS - On Success
 *             EXIT_FAILURE - On Failure
 */
static int handle_camera_record_pause(camera_record_pause_t *p_data)
{
    int           rc                                   = EXIT_FAILURE;
    int           is_recording                         = 0;
    camdef_t     *p_camdef;
    media_info_t *p_minfo                              = raconf_get_media_info(0);
    char          tmp[CAMERA_NAME_LENGTH + 1];
    static int sm_init = 0;

    if ( shutdownInProcess == TRUE)
    {
        syslog(LOG_NOTICE, "%s: Shutdown in process. Don't change anything.", __func__);
        return rc;
    }

#ifndef QNX_BUILD
    /* Initiate share_momery. it will happen only when record_pause message is received (SSD are mounted)
        shared memory will be created only when it is not created already*/
	if (sm_init == 0)
	{
		if (initialize_shared_memory())
	    {
	    	syslog(LOG_NOTICE,"%s:%d",__func__,__LINE__);
	        syslog(LOG_ERR,"***Failed to initialize reagent shared memory");
	    }
	    else
	    	sm_init = 1;
	}
#endif

    /* TODO: Function needs to be updated with an additional input parameter that
     * allows to Pause Recording to RSSD, CHM or BOTH, in order to
     * properly handle the different shutdown scenarios (i.e. Shutdown RSSD means
     * that the caddy door is open - this implies that we only need to stop recording
     * to the RSSD, NOT EVERYWHERE as it is currently being done).  May need to
     * re-factor this function.
     */

    if ((p_data != NULL) && (p_minfo != NULL))
    {
        memset(tmp, 0, sizeof(tmp));
        strncpy(tmp, p_data->camera, CAMERA_NAME_LENGTH);

        if (((p_camdef = raconf_find_camera(p_minfo, tmp)) != NULL) && (p_camdef->runtime.is_online) && (p_camdef->runtime.is_assigned))
        {
        	/*
        	 * according to Jyoti, the _main_ verb in 'CAMERA_RECORD_PAUSE' command mnemonic is
        	 * RECORD, so the "True" or "1" parameter means "Record!", "False" or "0" means "Pause!"
        	 */
        	is_recording = ((p_camdef->runtime.recording_status_flags & (DO_RECORD_CHM_VIDEO | DO_RECORD_SSD_VIDEO) )) ? 1 : 0 ;
            syslog(LOG_INFO, "Camera: %s, online: %d, assigned %d, recording %d", p_camdef->name, p_camdef->runtime.is_online, p_camdef->runtime.is_assigned, is_recording);

            if (is_recording != (int)p_data->record)
            {
                if (is_recording) /* i.e. we have to stop recording */
                {
                	p_camdef->runtime.recording_mode_flags = 0;
                    p_camdef->runtime.commanded_paused = 1;     // set commanded pause
                	rc = stop_recording_process(p_minfo,p_camdef);
                } // end if (is_recording)
                else if ( p_minfo->runtime.is_chm_ready || p_minfo->runtime.is_rssd_ready )
                {
                    p_camdef->runtime.commanded_paused = 0;     // set commanded record
                	rc = start_recording_process(p_minfo,p_camdef);
                }
            } // end if (is_recording != (int)p_data->record)
            else
            {
            	rc = EXIT_SUCCESS;
            	syslog(LOG_INFO, "Camera '%s' - nothing to do: current_recording_state=%d, commanded_recording_state=%d\n", tmp, is_recording, (int)p_data->record) ;
            }
        } // end if ( ((p_camdef = reagent_conf_find_camera(p_minfo, tmp)) != NULL) && () && ())
        else if (p_camdef != NULL)
        {
        	syslog(LOG_INFO, "Camera '%s' is offline/unassigned or not found\n", tmp) ;
            is_recording = ((p_camdef->runtime.recording_status_flags & (DO_RECORD_CHM_VIDEO | DO_RECORD_SSD_VIDEO) )) ? 1 : 0 ;
            syslog(LOG_INFO, "%s: Camera: %s, online: %d, assigned %d, recording %d", __func__, p_camdef->name, p_camdef->runtime.is_online, p_camdef->runtime.is_assigned, is_recording);

            send_video_application_error(eCameraOffline);
        }
        else
        {
            syslog(LOG_ERR, "%s: Could not retrieve camera definitions.\n", __func__);
            return EXIT_FAILURE;
        }
    } // end if ((p_data != NULL) && (p_minfo != NULL) )

    return (rc);
} //handle_camera_record_pause

/**
 * Function to send the Video Application Status Message to video_rec
 *
 * @param[in] status - Configuration State (Loco ID and Camera Config)
 *
 * @return    None
 */
void send_videoapp_status_msg(int status)
{
	va_video_application_status_t msg = {0};
    //va_video_application_status_t *p_data = (va_video_application_status_t *) p_info->p_data;

	msg.payload.header.version               = VIDEO_APPLICATION_STATUS_MSG_VERSION;
	msg.payload.configuration_info_available = status;

	PTC_SEND_MSG_WITH_PAYLOAD(VIDEO_APPLICATION_STATUS, msg, va_video_application_status_t);

} //send_videoapp_status_msg

/**
 * Function to send the Recording Status Message to video_rec
 *
 * @param[in] chm_status  - Status of CHM Recording
 * @param[in] rssd_status - Status of RSSD Recording
 *
 * @return    None
 */
void send_recording_status_msg(int chm_status, int rssd_status)
{
	va_recording_status_t msg = {0};

	msg.payload.header.version                          = RECORDING_STATUS_MSG_VERSION;
	msg.payload.recording_as_commanded_on_chm           = chm_status;
	msg.payload.recording_as_commanded_on_removable_ssd = rssd_status;

	PTC_SEND_MSG_WITH_PAYLOAD(RECORDING_STATUS, msg, va_recording_status_t);

} //send_recording_status_msg

/**
 * Function to send the Video Application Error Message to video_rec
 * when the Recording Agent encounters Errors
 *
 * @param[in] primaryErrorNumber - Video Application Error Message Structure
 *
 * @return    None
 */
void send_video_application_error(int primaryErrorNum)
{
    va_video_application_error_t msg = {0};

    char secondaryError[SECONDARY_ERROR_LENGTH] = {0};

    msg.payload.header.version       = VIDEO_APPLICATION_ERROR_MSG_VERSION;
    msg.payload.primary_error_number = (uint16_t)primaryErrorNum;

    switch(primaryErrorNum)
    {
        case eNoError:

            strcpy(secondaryError, "No Error.");
            break;

        case eCameraOffline:

            strcpy(secondaryError,"Cannot detect camera, camera offline.");
            break;

        case eCameraNoDataRecvd:

            strcpy(secondaryError, "Camera connection bad, camera online but data not received.");
            break;

        case eCameraSetupFailure:

            strcpy(secondaryError, "Camera connection bad, camera online by RTSP setup failure.");
            break;

        case eCameraPlayFailure:

            strcpy(secondaryError, "Camera connection bad, camera online but RTSP play failure.");
            break;

        case eCameraTooManyUsers:

            strcpy(secondaryError, "Camera connection bad, too many users connected with the camera.");
            break;

        case eCameraBandwidthNarrow:

            strcpy(secondaryError, "Camera connection bad, bandwidth is narrow.");
            break;

        case eCameraSequenceBreak:

            strcpy(secondaryError, "Camera connection bad, sequence break detected.");
            break;

        case eCameraInvalidCredentials:

            strcpy(secondaryError, "Cannot configure camera, invalid camera credentials.");
            break;

        case eCameraUnableToSendData:

            strcpy(secondaryError, "Cannot configure camera, unable to send data to a camera.");
            break;

        case eAudioUnavailable:

            strcpy(secondaryError, "Audio unavailable.");
            break;

        case eChmNotPresent:

            strcpy(secondaryError, "CHM error, CHM not present.");
            break;

        case eChmNotWritable:

            strcpy(secondaryError, "CHM error, unable to record on CHM.");
            break;

        case eRssdNotPresent:

            strcpy(secondaryError, "Removable SSD error, removable SSD is not present.");
            break;

        case eRssdNotWritable:

            strcpy(secondaryError, "Removable SSD error, unable to record on removable SSD.");
            break;

        case eLocoIdMismatch:

            strcpy(secondaryError, "Locomotive ID mismatch, locomotive ID mismatch occurred.");
            break;

        case eCameraSettingsFailure:

            strcpy(secondaryError, "Camera Settings Failure, settings of camera not successful.");
            break;

        case eCameraNameMismatch:

            strcpy(secondaryError, "Camera names mismatch, Camera names does not match.");
            break;

        case eCameraNotAvailable:

            strcpy(secondaryError, "Camera not available, camera is not available.");
            break;

        case eCameraUnassigned:

            strcpy(secondaryError, "Unassigned camera, camera is unassigned.");
            break;

        case eSetFrameRateFailure:

            strcpy(secondaryError, "Frame rate setting failure, frame rate not set successfully.");
            break;

        case eSetResolutionFailure:

            strcpy(secondaryError, "Resolution setting failure, resolution not set successfully.");
            break;

        case eSetCompressionFailure:

            strcpy(secondaryError, "Compression setting failure, compression not set successfully.");
            break;

        case eSetAudioFailure:

            strcpy(secondaryError, "Audio Setting Failure, Camera audio is not set successfully.");
            break;

    } // end switch(msg.payload.primary_error_number)

    memcpy(msg.payload.secondary_error, secondaryError, SECONDARY_ERROR_LENGTH);

    PTC_SEND_MSG_WITH_PAYLOAD(VIDEO_APPLICATION_ERROR, msg, va_video_application_error_t);
    syslog(LOG_NOTICE,"%s: Camera error primary_error_number %d secondary_error %s",__func__,primaryErrorNum,secondaryError);

} //send_video_application_error

/**
 * stop_recording_process
 *
 * @param[in] *p_minfo Pointer to structure 
 * @param[in] *p_camdef Pointer to structure 
 * @return     EXIT_SUCCESS/EXIT_FAILURE
 */
int stop_recording_process(media_info_t *p_minfo, camdef_t *p_camdef)
{

	int status = EXIT_FAILURE ;
	if ((p_minfo == NULL) || (p_camdef == NULL))
    {
        return status;
    }
	if (p_camdef->worker_pid) {
	    kill(p_camdef->worker_pid, SIGQUIT);

		waitpid(p_camdef->worker_pid, &status, 0) ;
	    syslog(LOG_INFO, "%s: Process %d waitpid() returned status %d\n", __func__, p_camdef->worker_pid, WEXITSTATUS(status));

		if (WIFEXITED(status)) {
			syslog(LOG_INFO, "%s: exited, status=%d\n", __func__,
					WEXITSTATUS(status));
		} else if (WIFSIGNALED(status)) {
			syslog(LOG_INFO, "%s: killed by signal %d\n", __func__,
					WTERMSIG(status));
		} else if (WIFSTOPPED(status)) {
			syslog(LOG_INFO, "%s: stopped by signal %d\n", __func__,
					WSTOPSIG(status));
		} else if (WIFCONTINUED(status)) {
			syslog(LOG_INFO, "%s: continued\n", __func__);
		}

		camera_recording_just_stopped(p_minfo, p_camdef) ;
		p_camdef->worker_pid = 0;

	if (p_camdef->we.lwe_wordv) {
		l_wordfree(&(p_camdef->we)) ;
		memset(&(p_camdef->we), 0, sizeof(p_camdef->we)) ;
	}

		//Only re-initiate space_monitor if camera streaming going down has grooming configured
		if ((p_camdef->max_age[0] != '\0') && (atoi(p_camdef->max_age) > 0)) {
			syslog(LOG_NOTICE,
					"Initiating kill of space monitor from %s due to %s camera going down and max age is %s\n",
					__func__, p_camdef->name, p_camdef->max_age);
			stop_space_monitor(p_minfo);
		} else {
			syslog(LOG_NOTICE,
					"Not Initiating kill of space monitor from %s due to %s camera going down and max age is %s\n",
					__func__, p_camdef->name, p_camdef->max_age);
		}

		status = EXIT_SUCCESS ;
	}
	else {
		syslog(LOG_INFO, "%s: Stop recording: no running recording process for camera '%s'", __func__, p_camdef->name);
	}

	return status ;
}

/**
 * start_recording_process
 *
 * @param[in] *p_minfo Pointer to structure 
 * @param[in] *p_camdef Pointer to structure 
 * @return     EXIT_SUCCESS/EXIT_FAILURE
 */
int start_recording_process(media_info_t *p_minfo, camdef_t *p_camdef)
{
	pid_t       pid;
	int         rc                    = 0;
	int         status                = 0;
	int         cid                   = 0;
	char        tmp[CAMERA_INFO_LEN]  = {0};
	static int  retry_count[MAX_CAMS] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	camdef_t   *p;
	int         i                     = 0;

    if ((p_minfo == NULL) || (p_camdef == NULL))
    {
        return EXIT_FAILURE;
    }

    memset(tmp, 0, sizeof(tmp));
    strncpy(tmp, p_camdef->name, CAMERA_NAME_LENGTH);
    trunc_name(&tmp[0], CAMERA_NAME_LENGTH) ;

    int n = p_minfo->n_cameras ;
	for (i=0; i < n; i++) {
		p = &p_minfo->cameras[i] ;
		if (strncasecmp(p_camdef->name,p->name, CAMERA_NAME_LENGTH) == 0)
		{
			cid = i;
			break;
		}
	}

	if (retry_count[cid] > 2)
	{
		/*if we are here, IT IS VERY BAD. it means we already have 3 consecutive tries to start recording but failed
		No more tries */
		/*FIX ME: To stop more tries, tagging this camera as un-assigned camera */
		p_camdef->runtime.is_assigned = 0;
		syslog(LOG_ERR,"*** Failed to start recording for camera %s in 3 tries. Giving up Now!!!! ***",tmp);
		return (EXIT_FAILURE);
	}

	syslog(LOG_NOTICE,"%s: %s",__func__,p_camdef->name);

	/* set the recording mode*/
	if (p_camdef->runtime.is_essential != 0) {
		// Default record to CHM if essential
		p_camdef->runtime.recording_mode_flags |= DO_RECORD_CHM_VIDEO ;
		if (str2bool(p_camdef->do_audio) != 0) {
			p_camdef->runtime.recording_mode_flags |= DO_RECORD_CHM_AUDIO ;
		}
	}
	else
	{
		p_camdef->runtime.recording_mode_flags |= DONT_USE_CHM;
	}
	// Default record to RSSD
	p_camdef->runtime.recording_mode_flags |= DO_RECORD_SSD_VIDEO ;
	if (str2bool(p_camdef->do_audio) != 0) {
		p_camdef->runtime.recording_mode_flags |= DO_RECORD_SSD_AUDIO ;
	}

	/* we have to start recording from this camera. Do extra checks first */
	if ((p_camdef->runtime.recording_mode_flags & (DO_RECORD_CHM_VIDEO | DO_RECORD_SSD_VIDEO)) != 0) {
		if ((p_camdef->runtime.recording_mode_flags & DO_RECORD_CHM_VIDEO) && (p_minfo->runtime.is_chm_ready==0)) {
			syslog(LOG_ERR, "Camera '%s': CHM is not available for recording\n", tmp);
			retry_count[cid] = 0;
			return (EXIT_FAILURE);
		}
		if ((p_camdef->runtime.recording_mode_flags & DO_RECORD_SSD_VIDEO) && (p_minfo->runtime.is_rssd_ready==0)) {
			 p_camdef->runtime.recording_mode_flags &= (~(DO_RECORD_SSD_VIDEO | DO_RECORD_SSD_AUDIO)) ;
			syslog(LOG_ERR, "Camera '%s': RSSD is not available for recording\n", tmp);
		}
	}
	else {
		syslog(LOG_ERR, "Recording from camera '%s' is disabled -- cannot start\n", tmp);
		retry_count[cid] = 0;
		return (EXIT_FAILURE);
	}

    raconf_prepare_for_recording(p_minfo, p_camdef ) ;
    if ((p_camdef->runtime.is_assigned) && ((p_camdef->worker_pid == 0) || (p_camdef->worker_pid == -1)))
    {
    	syslog(LOG_INFO, "%s: Start recording for this camera %s current worker pid %d", __func__, p_camdef->name, p_camdef->worker_pid);
#ifdef QNX_BUILD
    	{
    		static char cmd[1024];
    		int cnt, pos ;


    		pos = 0 ;
    		for (cnt=0; cnt < p_camdef->we.lwe_wordc; cnt++) {
    			pos += sprintf(&cmd[pos], "%s ", &(p_camdef->we.lwe_wordv[cnt][0])) ;
    		}
    		syslog(LOG_INFO, "%s: *** %s", __func__, cmd);
    	}
#endif
    	pid = spawn_process(p_camdef->we.lwe_wordc, p_camdef->we.lwe_wordv);

	    if (pid != -1)
		{
			retry_count[cid] = 0;
			syslog(LOG_INFO, "%s: rtsprec process started. pid %d", __func__, pid);                            
			p_camdef->worker_pid = pid;

			p_camdef->runtime.recording_status_flags = 0 ;
			if (p_camdef->runtime.recording_mode_flags & DO_RECORD_CHM_VIDEO) {
				p_camdef->runtime.recording_status_flags |= IS_RECORDING_CHM_VIDEO ;
				if (p_camdef->runtime.recording_mode_flags & DO_RECORD_CHM_AUDIO) {
					p_camdef->runtime.recording_status_flags |= IS_RECORDING_CHM_AUDIO ;
				}
			}
			if (p_camdef->runtime.recording_mode_flags & DO_RECORD_SSD_VIDEO) {
				p_camdef->runtime.recording_status_flags |= IS_RECORDING_SSD_VIDEO ;
				if (p_camdef->runtime.recording_mode_flags & DO_RECORD_SSD_AUDIO) {
					p_camdef->runtime.recording_status_flags |= IS_RECORDING_SSD_AUDIO ;
				}
			}

			/* reflect the total number of cameras currently being recorded */
			p_minfo->runtime.n_cams_recording += 1 ;

			rc =  EXIT_SUCCESS;

            send_camera_status(p_camdef);
            status = see_if_recording(&(p_minfo->cameras[0]), p_minfo->n_cameras);
            send_recording_status_msg(status & 1, status & 2);
            syslog(LOG_INFO, "%s: Successfully started recording from camera '%s'", __func__, tmp);
            // Only re-initiate space_monitor if new camera streaming has grooming configured
            if( (p_camdef->max_age[0] != '\0') &&  (atoi(p_camdef->max_age) > 0))
            {
                syslog(LOG_NOTICE, "Initiating kill of space monitor from %s due to %s camera streaming and max age is %s\n", __func__, p_camdef->name, p_camdef->max_age);
                stop_space_monitor(p_minfo);
            }
            else
            {
                syslog(LOG_NOTICE, "Not Initiating kill of space monitor from %s due to %s camera streaming and max age is %s\n", __func__, p_camdef->name, p_camdef->max_age);
            }

		} // end if (pid != -1)
	    else
	    {
		    retry_count[cid] += 1;
		    syslog(LOG_ERR, "Failed to start recording from camera '%s'", tmp);
		    rc = EXIT_FAILURE;
	    }
    } // end if ((p_camdef->runtime.is_assigned) && ((p_camdef->worker_pid == 0) || (p_camdef->worker_pid == -1)))

	handle_metarec(p_minfo);
	handle_space_monitor(p_minfo);
	return (rc);
} // start_recording_process

/**
 * send_camera_status
 *
 * @param[in] *p_camdef Pointer to structure 
 * @return     None
 */
void send_camera_status(camdef_t *p_camdef)
{
	camera_status_t cameraStatus;

    int  resh                 = 0;
    int  resw                 = 0;
    int  compr                = 0;
    int  frate                = 0;
    char tmp[CAMERA_INFO_LEN] = {0};

	memset(&cameraStatus, 0, sizeof(cameraStatus)) ;
	cameraStatus.header.version = CAMERA_STATUS_MSG_VERSION ;

    memcpy((char *)cameraStatus.camera, (char *)p_camdef->name, CAMERA_NAME_LENGTH);
    trunc_name(&(cameraStatus.camera[0]), CAMERA_NAME_LENGTH);
    memcpy((uint8_t *)cameraStatus.camera_serial, (uint8_t *)p_camdef->disp_mac, CAMERA_SERIAL_NUM_LENGTH);
    memcpy(cameraStatus.camera_model, p_camdef->model, CAMERA_MODEL_INFO_LENGTH);                          

	cameraStatus.resolution  = (uint8_t)p_camdef->runtime.resolution;
	cameraStatus.compression = (uint8_t)p_camdef->runtime.compression;
	cameraStatus.frame_rate  = (uint8_t)p_camdef->runtime.frame_rate;

	if (p_camdef->runtime.recording_status_flags)
	{
	    if (ovc_get_extended_info(p_camdef->homepage, &resh, &resw, &compr, &frate) == EXIT_SUCCESS)
	    {
	        memset( (char *) tmp, 0, sizeof(tmp));
	        sprintf(tmp, "%dx%d", resw, resh);
	        strncpy(cameraStatus.actual_resolution, tmp, CAMERA_RESOLUTION_LENGTH);
	        cameraStatus.actual_compression = (uint8_t) compr;
	        cameraStatus.actual_framerate   = (uint8_t) frate;
	    }
	    else
	    {
	        syslog(LOG_ERR, "%s: Unable to get camera extended parameters.", __func__);
	        memset((char *) cameraStatus.actual_resolution, 0, CAMERA_RESOLUTION_LENGTH);
	        cameraStatus.actual_compression = 0;
	        cameraStatus.actual_framerate   = 0;
	    }
	}
	cameraStatus.no_audio_recording = (bool)!(p_camdef->runtime.recording_status_flags & (IS_RECORDING_CHM_AUDIO | IS_RECORDING_SSD_AUDIO));
	cameraStatus.no_chm_recording   = (bool)!(p_camdef->runtime.recording_status_flags & IS_RECORDING_CHM_VIDEO);
	cameraStatus.accessory_power_on = 0 ;
	cameraStatus.recording          = (bool) p_camdef->runtime.recording_status_flags & (IS_RECORDING_CHM_VIDEO | IS_RECORDING_SSD_VIDEO);
	cameraStatus.camera_online      = (bool) ((p_camdef->runtime.is_assigned) && (p_camdef->runtime.is_online));
	cameraStatus.camera_assignment  = (bool) p_camdef->runtime.is_assigned;

	sendCameraStatus(cameraStatus);

	return;
} //send_camera_status


int get_assigned_camera_count(media_info_t *p_minfo)
{
	camdef_t *p_camdef;

	int acc = 0;
	int n = p_minfo->n_cameras ;
	int i ;

	for (i=0; i < n; i++) {
		p_camdef = &p_minfo->cameras[i] ;
		if (p_camdef->runtime.is_assigned)
		{
			acc +=1;
		}
	}
	return (acc);
}
