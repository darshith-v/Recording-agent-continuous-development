/**
 * @file    ptc_intf.h
 *
 * @author  WRE SW Engineer
 *
 * @section DESCRIPTION
 *
 * Header file containing functions to
 * initialize, start and stop the PTC Client
 *
 * @section COPYRIGHT
 *
 * Copyright 2019-2020 WABTEC Railway Electronics
 * This program is the property of WRE.
 */

#ifndef _PTC_INTF_H_
#define _PTC_INTF_H_

#include <stdint.h>
#include <stdbool.h>

#include "reagent_config.h"

//////////// handle_ipc_msg.h ///////////////

enum
{
	eNoError                  = 0,
	eCameraOffline            = 1,
	eCameraNoDataRecvd        = 2,
	eCameraSetupFailure       = 3,
	eCameraPlayFailure        = 4,
	eCameraTooManyUsers       = 5,
	eCameraBandwidthNarrow    = 6,
	eCameraSequenceBreak      = 7,
	eCameraInvalidCredentials = 8,
	eCameraUnableToSendData   = 9,
	eAudioUnavailable         = 10,
	eChmNotPresent            = 11,
	eChmNotWritable           = 12,
	eRssdNotPresent           = 13,
	eRssdNotWritable          = 14,
	eLocoIdMismatch           = 16,
	eCameraNameMismatch       = 17,
	eCameraNotAvailable       = 18,
	eCameraUnassigned         = 19,
	eCameraSettingsFailure    = 21,
	eSetFrameRateFailure      = 23,
	eSetResolutionFailure     = 25,
	eSetCompressionFailure    = 27,
	eSetAudioFailure          = 29
};

enum
{
	eNoCamerasRecording = 0,
	eRecordingToChm     = 1,
	eRecordingToRssd    = 2,
	eRecordingToBoth    = 3
};

typedef struct sStatusDescriptor
{
	bool isMessageSent;
	bool hasAckBeenReceived;
}status_descr_t;

typedef struct sPtcMessages
{
	status_descr_t vidAppStatus;
	status_descr_t recStatus;
	status_descr_t vidAppError;
	status_descr_t downloadAck;
	status_descr_t downloadStatus;
	status_descr_t shutdownStatus;
	status_descr_t heartbeatResp;
	status_descr_t cameraStatus;
}ptcMessages_t;

int  str2bool                    (char                  *p);
void send_videoapp_status_msg    (int                    status);
void send_recording_status_msg   (int                    chm_status,
                                  int                    rssd_status);
void send_video_application_error(int                    primaryErrorNum);
/////////////

int  ptc_client_init (int argc, char *argv[]);
void ptc_client_run  (void *xtra_state);
void ptc_client_stop ();

/**
 * send_camera_status
 *
 * @param[in] *p_camdef Pointer to structure 
 * @return     None
 */
void send_camera_status(camdef_t *p_camdef);

#endif
