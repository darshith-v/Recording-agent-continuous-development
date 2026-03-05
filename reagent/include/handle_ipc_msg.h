/**
 * @file    handle_ipc_msg.h
 *
 * @author  WRE SW Engineer
 *
 * @section DESCRIPTION
 *
 * Header file for handle_ipc_msg.h
 *
 * @section COPYRIGHT
 *
 * Copyright 2019 WABTEC Railway Electronics
 * This program is the property of WRE.
 */

#ifndef _HANDLE_IPC_MSG_
#define _HANDLE_IPC_MSG_

#include <stdbool.h>

#include "reagent_config.h"
#include "video_api.h"

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
int  handle_camera_record_pause  (camera_record_pause_t *p_data);
void send_videoapp_status_msg    (int                    status);
void send_recording_status_msg   (int                    chm_status, 
                                  int                    rssd_status);
void send_video_application_error(int                    primaryErrorNum);
int  see_if_recording            (camdef_t              *p_camdef,
                                  int                    ncams);
#endif
