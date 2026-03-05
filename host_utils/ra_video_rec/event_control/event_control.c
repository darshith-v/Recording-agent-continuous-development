/**
 * @file   event_control.c
 *
 * @author Michael Nguyen
 *
 * @section DESCRIPTION
 *
 * Manages video event control
 *
 * @section COPYRIGHT
 *
 * Copyright 2017-2020 WABTEC Railway Electronics
 * This program is the property of WRE. 
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/io.h>                     // for inb and outb (reading FPGA)
#include <errno.h>                      // system error code
#include <sys/mount.h>
#include <fstab.h>

#ifndef QNX_BUILD
#include <mntent.h>
#endif	// #ifndef QNX_BUILD

#include <paths.h>
#include <string.h>
#include <dirent.h>
#include <stdbool.h>
#include <unistd.h>
#include <syslog.h>
#include <pthread.h>
#include <sys/statvfs.h>

#include "arg_utils.h"                  // For arg_opt_*() calls
#include "crc_utils.h"                  // For crc32_*() calls
#include "exit_codes.h"                 // For EXIT_PTC_* codes
#include "logger.h"                     // For LOGF() calls
#include "ptc_api.h"                    // For ptc_*() calls
#include "video_api.h"                  // For message structure definitions
#include "video_rec.h"                  // Function prototypes.
#include "ptc_message.h"

#include "ldars_configuration.h"

#ifndef QNX_BUILD
#include "ldars_timer.h"
#include "shmem_utils.h"
#include "LnvrNasShm.h"
#include "ldrs_v_removable_ssd_util.h"
#include "video_recording_monitor.h"
#include "file_dir_utilities.h"
#endif	// #ifndef QNX_BUILD


extern bool g_LNVR;
extern bool g_NasConfigured;

bool g_FirstTime =  true;
int  g_tries     = -1;


// Global data structure
extern sVideoRecInfo *pVideoRecInfo;
extern struct s_monitor_data monitorData;

#define RESTART_API                          2
#define NUM_DISCRETE_INPUTS                  3
#define UNINITIALIZED_DISCRETES              99
#define TIME_2SEC                            2
#define RETRY_LIMIT                          10
#define NUM_INTERATIONS_BEFORE_CHECKING_PSON 3  // check_for_event occurs every 100ms
                                                // in order to lighten the load on shared
                                                // memory, we only want to check the PSON
                                                // flag every 300ms.
#define WRE_RECORDING_MONITOR_TIMER          50  // 50*100ms = 5 Secs

#define CHECK_CHM_CONNECT_THRESHOLD          10 // check the chm connect every second 

typedef union __attribute__((__packed__))
{
	struct
	{
		uint8_t    input_1 : 1; // LSB -Bit 0 - Input 1
		uint8_t    input_2 : 1; //     -Bit 1 - Input 2
		uint8_t    input_3 : 1; //     -Bit 2 - Input 3
		uint8_t    spare_4 : 1; //     -Bit 3 - Not Used
		uint8_t    spare_5 : 1; //     -Bit 4 - Not Used
		uint8_t    spare_6 : 1; //     -Bit 5 - Not Used
		uint8_t    spare_7 : 1; //     -Bit 6 - Not Used
		uint8_t    spare_8 : 1; // MSB -Bit 7 - Not Used
	}bits;

	uint8_t value;
}sDiscreteInputReg;

static video_event_info_t   eventControlMsg;
static sDiscreteInputReg    discreteFpgaReg;              // current discrete data
static sDiscreteInputReg    prevDiscreteFpgaReg;          // previous discrete data
#ifdef LNVR
static BOOL                 checkMount           = FALSE;
#else
static BOOL                 checkMount           = TRUE;
#endif
ready_status_t              ready_status;
static sLDARSConfiguration *pConfig              = NULL;
// define a structure to handle an expired discrete event timer
typedef struct
{
    timer_id_t                       timer_id;
    sVInputForEventControlledCamera *eventInputPtr; // camera event inputs
    sVCameraConfiguration           *camConfigPtr;  // camera configuration pointer
}sDiscreteEventTimerStruct;

// create a discrete event timer pool to handle delayed events
sDiscreteEventTimerStruct discreteInputTimerArr[MAX_NUMBER_OF_CAMERA_CONFIGURATIONS];
/*we start the video recording monitor assuming all cameras are recording*/
/*Making this global in-case someone need this in future*/
struct sCameraRecordingStatus  cameraRecordingStatus[NUM_CAMERAS] = {{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE},{TRUE,TRUE}}; 

#ifndef LNVR
static pthread_t rSSDThreadID = -1;
static bool      startup      = true;
#endif

// function prototypes
int         process_discrete_input_data               (uint8_t  numEvents,
                                                       uint8_t *eventPtr);
int         video_rec_send_video_event_info           (const char *event_name,
                                                       uint8_t     event_state);

void        process_nas_event                         (uint32_t action);
static void check_for_event                           (timer_id_t  tid,
                                                       INT32       xcount,
                                                       void       *p_user);
static void discreteInputTimerEventHandler            (timer_id_t  tid,
                                                       INT32       xcount,
                                                       void       *p_user);
#ifndef LNVR
static void holdOffMountTimerEventHandler             (timer_id_t  tid,
                                                       INT32       xcount,
                                                       void       *p_user);
static void mountRemovableSSDTimerEventHandler        (timer_id_t  tid,
                                                       INT32       xcount,
                                                       void       *p_user);
#endif
void        performDiscreteInputAction                (sVCameraConfiguration           *camConfigPtr,
                                                       sVInputForEventControlledCamera *eventInputPtr,
                                                       uint8_t                          cameraConfigIndex);
void        monitorGPSDataUpdateinSHMTimerEventHandler(timer_id_t  tid,
                                                       INT32       xcount,
                                                       void       *p_user);
int         video_rec_send_gps_data                   (const char *latitude,
                                                       const char *longitude,
                                                       const char * speed);
void        *chmSSDMountHandler                       (void *ptr); 
#ifndef LNVR
// Removable SSD mount prototypes.
void  startRemovableSSDMountProcess(void);
void *removableSSDThreadhandler    (void *info);
#endif

/**
 * Initialize event control
 *
 * @return None
 *
 */
int init_event_control(void)
{
    int                  index           = 0;
    uint8_t              numberOfCameras = 0;

    uint8_t              rc              = EXIT_FAILURE;

    syslog(LOG_DEBUG, "%s:%s[%d]", __FILE__, __FUNCTION__, __LINE__);

    LOGF(LDEBUG, "Event Control Init.\n");

    discreteFpgaReg.value     = UNINITIALIZED_DISCRETES;
    prevDiscreteFpgaReg.value = UNINITIALIZED_DISCRETES;

    memset(&eventControlMsg, 1 ,sizeof(video_event_info_t));

    memset(eventControlMsg.name, '\0', EVENT_NAME_LENGTH);

    // get LDRS configuration from shared memory
    rc = getLocallyStoredConfiguration(&pConfig);

    if ((pConfig != NULL) && (rc == EXIT_SUCCESS))
    {
        numberOfCameras = pConfig->configuration.numCameraConfigurations;
#ifdef DEBUG
        syslog(LOG_DEBUG, "%s: Number of Cameras configured %d ", __func__, numberOfCameras);
#endif
    }
    else
    {
        syslog(LOG_CRIT,"%s: Unable to retrieve configuration. ", __func__);
        return EXIT_FAILURE;
    }

#ifndef QNX_BUILD
    // Initialize the Discrete Input delayed action timer array
    for (index = 0; index < numberOfCameras; index++)
    {
        // create timers.
        ptc_timer_create((timer_id_t *) &discreteInputTimerArr[index].timer_id, discreteInputTimerEventHandler, NULL);

        // initialize the event input pointer
        discreteInputTimerArr[index].eventInputPtr = NULL;

        // initialize the camera configuration pointer
        discreteInputTimerArr[index].camConfigPtr = NULL;
    }
#endif	// #ifndef QNX_BUILD

#ifdef LNVR
    ptc_timer_create((timer_id_t *) &pVideoRecInfo->videoShutdownTimer, nasVideoShutdownTimerEventHandler, NULL);
#else
    // create timer for governing when SHUTDOWN command is sent based on open/close door events
    ptc_timer_create((timer_id_t *) &pVideoRecInfo->videoShutdownTimer, videoShutdownTimerEventHandler, NULL);
#ifndef QNX_BUILD
    // create timer for governing when mount command is sent when door is locked
    ptc_timer_create((timer_id_t *) &pVideoRecInfo->holdoffMountTimer, holdOffMountTimerEventHandler, NULL);    
    ptc_timer_create((timer_id_t *) &pVideoRecInfo->mountRemovableSSDTimer, mountRemovableSSDTimerEventHandler, NULL);  
    ptc_timer_create((timer_id_t *) &pVideoRecInfo->unmountRemovableSSDTimer, unmountRemovableSSDTimerEventHandler, NULL);      
#endif	// #ifndef QNX_BUILD    
    pVideoRecInfo->shutdownStarted = FALSE; // set flag to indicate no shutdown process is currently running.
#endif

#ifndef QNX_BUILD
    // Create timer for monitoring whether GPS data has been updated in shared memory.
    ptc_timer_create((timer_id_t *) &pVideoRecInfo->monitorGPSDataSHMTimer, monitorGPSDataUpdateinSHMTimerEventHandler, NULL);
#endif	// #ifndef QNX_BUILD
    return (EXIT_SUCCESS);

} //init_event_control

/**
 * Start event control
 *
 * @param[in] config_info_avail - indicates whether configuration info is available
 *
 * @return Success or Failure
 *
 */
int start_event_control(BOOL config_info_avail)
{
    // event processing timer that controls 10 hz discrete and door lock polling
    timer_id_t eventControl10HzProcessingTid = -1;
    int        retStatus                     =  0;
    
    LOGF(LDEBUG, "Start Event Control\n");

    syslog(LOG_DEBUG, "%s:%s[%d]", __FILE__, __FUNCTION__, __LINE__);    
        
    // create the main 10Hz event control processing timer
    retStatus = ptc_timer_create(&eventControl10HzProcessingTid, check_for_event, NULL);

    if (retStatus == SUCCESS)
    {
        // start the 10Hz processing timer
        ptc_timer_start(eventControl10HzProcessingTid,
                        EVENT_CONTROL_PROCESSING_TIMEOUT_INITIAL,
                        EVENT_CONTROL_PROCESSING_TIMEOUT_RELOAD);
    }
    else
    {
        return (EXIT_FAILURE);
    }

    // Start timer for monitoring whether GPS data has been updated in shared memory.
    ptc_timer_start(pVideoRecInfo->monitorGPSDataSHMTimer,
                    MONITOR_GPS_UPDATE_TIMEOUT_INITIAL,
                    MONITOR_GPS_UPDATE_TIMEOUT_RELOAD);

    return (EXIT_SUCCESS);
} //start_event_control

/**
 * 10HZ Timer callback routine, monitors the caddie door and reads the 
 * Discrete Input register from the FPGA. It detects any change
 * in state.
 *
 * @param[in] tid     - 10HZ processing timer identifier
 * @param[in] xcount  - number of times the timer expired since last callback
 * @param[in] *p_user - pointer to a user defined input parameter (not used)
 *
 * @return None
 *
 */

static void check_for_event(timer_id_t tid, INT32 xcount, void *p_user)
{
    unsigned char  regData                      = 0;
    bool           doThemAll                    = false;
    const uint8_t  allSet                       = 7;     // fake the exclusive OR to all 3 bits changed
    static uint8_t power_down_check_shm_counter = 0;
    static BOOL    power_going_down             = FALSE;
    va_ready_status_t  readymsg;    

#ifdef LNVR
    sLnvrNasStatus shmNasStatus;
#endif
    sSharedMemData sharedMemValue;

    // First, increment the counter.  Once the counter has incremented to NUM_INTERATIONS_BEFORE_CHECKING_PSON (300 ms),
    // We will check shared memory to ensure that the PSON bit is not set.
    power_down_check_shm_counter++;
#ifndef QNX_BUILD
    // First check if power is going down. Check every time this routine is accessed, i.e. check every 300 ms.
    // Check when running in normal operation. Once the shutdown message is sent, no need to check this again.
    if ((NUM_INTERATIONS_BEFORE_CHECKING_PSON == power_down_check_shm_counter) && (FALSE == power_going_down))
    {
        if (readWriteSharedMemoryValue(eSHM_Type_fpga_Powerdown_Indicator, &(sharedMemValue.videoRec.fpga_Powerdown_Indicator), eSHM_Read) == EXIT_SUCCESS)
        {
            if (TRUE == sharedMemValue.videoRec.fpga_Powerdown_Indicator)
            {
                pVideoRecInfo->vraShutdownInProgress = TRUE;  // set flag to indicate shutdown process has started.
                pVideoRecInfo->driveStatusRequested = TRUE;
                power_going_down = TRUE; // To prevent us from going back into this condition during the next
                                         // 300 ms iteration.
            }
            else
            {
                power_down_check_shm_counter = 0; // If PSON is not set, reset the counter to zero
            }
        }
        else
        {
            syslog(LOG_ERR, "%s: Unable to read shared memory value for fpga_Powerdown_Indicator", __func__);
            power_down_check_shm_counter = 0; // If we are unable to access shared memory, reset the counter to
                                              // zero so we check during the next 300 ms iteration.
        }
    }
#endif //#ifndef QNX_BUILD
    // LNVR is a special case - NAS can be configured or not. If not use CHM

    // check to see do we need to send READY_STATUS 
    if (pVideoRecInfo->driveStatusRequested == TRUE)
    {
        pVideoRecInfo->driveStatusRequested = FALSE;
        if (power_going_down == TRUE)
        {
            memset((char *) &readymsg, 0, sizeof(readymsg));
            readymsg.payload.header.version = READY_STATUS_MSG_VERSION;

            // Update the payload with the drives status
            readymsg.payload.chm_ready = FALSE;
            readymsg.payload.removable_ssd_ready = FALSE;
            send_readystatus_vra((ready_status_t *) &readymsg);
            usleep(ONE_HUNDRED_MS_IN_USEC);       // Note this routine is called every 100 ms. Timing issues here?

            // Now send shutdown request.
            sendShutdownPowerLoss();
            return;
        }
        else
        {
            send_readystatus_vra((ready_status_t *) &(pVideoRecInfo->driveReadyStatus));
        }

    }

#ifndef QNX_BUILD
	/* monitor chm mount ready flag */
    process_chm_connect_status();
#ifndef LNVR
    process_door_lock_status();
#else
    if (power_going_down == FALSE)
    {

        // See if one of our 2 command flags has been set inform Reagent, clear when Reagent responds
        if (g_NasConfigured && ((++g_tries)%RETRY_LIMIT == 0) )  // this is  10Hz timer so slow it down 
        {
            g_tries =0;

            lnvrNasUpdateSharedMem(eSHM_GET, eSHM_NAS_STATUS, (void *)&shmNasStatus);  // Read the value
            // don't call process_nas_event if it is still being handled by the timer

            if ( pVideoRecInfo->vraShutdownInProgress == FALSE &&
                 shmNasStatus.lnvrVideoStop == 1 &&
                 ptc_timer_active(pVideoRecInfo->videoShutdownTimer) == FALSE )  
            {
                syslog(LOG_NOTICE, "%s[%d] LNVR NAS REQUEST: to shutdown the video recording on NAS",__FUNCTION__,__LINE__);
                process_nas_event(SHUTDOWN);
            }
            //lnvr_nas asked to start video recording... 
            if ( shmNasStatus.lnvrVideoStart == 1)
            {
                
                syslog(LOG_NOTICE, "%s[%d]:LNVR NAS REQUEST: Send READY command to VRA.", __func__,__LINE__);

                process_nas_event(READY_STATUS);

                // we really don't care if recoring starts before we mount, just that Reagent is told (once)
                lnvrNasUpdateSharedMem(eSHM_SET, eVIDEO_REC_CLEAR_FLAG, NULL);
            } 
            else if ( shmNasStatus.lnvrVideoStart == RESTART_API)
            {
                syslog(LOG_NOTICE, "%s[%d]:LNVR NAS REQUEST: Restarting Video rec.", __func__,__LINE__);
                process_nas_event(RESTART_API);

            }     
        }
    }
#endif
	/* make sure we are not in shutting down and digital configuration has been sent */
    if ((power_going_down == FALSE) && (pVideoRecInfo->isDigitalConfigSent == TRUE))
    {

        regData = getDiscreteInputRegisterData();

        if (regData != DIN_REG_DATA_UNKNOWN)
        {
            if (prevDiscreteFpgaReg.value == UNINITIALIZED_DISCRETES)
            {
                // First time through act on all discretes
                doThemAll = true;
            }
            
            discreteFpgaReg.value     = (regData) & (DISCRETE_INPUT_REG_MASK);

            if ( (discreteFpgaReg.value != UNINITIALIZED_DISCRETES) &&
                 ((prevDiscreteFpgaReg.value != discreteFpgaReg.value) || (doThemAll)))
            {
                // new value in FPGA ... need to process the data
                uint8_t index;
                uint8_t numNewEvents                     =  0;
                uint8_t newEventArr[NUM_DISCRETE_INPUTS] = {0, 0, 0};
                uint8_t temp;

                if (doThemAll)
                {
                    temp      = allSet;
                    doThemAll = false;
                }
                else
                {
                    // find the differences
                    temp = (discreteFpgaReg.value ^ prevDiscreteFpgaReg.value);             
                }
                // save current value to previous
                prevDiscreteFpgaReg.value = discreteFpgaReg.value ;
                
                // create the new events array
                for (index = 0; index < NUM_DISCRETE_INPUTS; index++)
                {
                    // check each discrete input
                    if (temp & (1 << index))
                    {
                        // the discrete input has changed state, mark for processing
                        newEventArr[numNewEvents++] = index + 1;
                    }
                }
                // Process the changed discrete input(s)
                process_discrete_input_data(numNewEvents, &newEventArr[0]);
            }

        } // end if register is in an unknown state
    }
#endif	// #ifndef QNX_BUILD
} // check_for_event



/**
 * Determine whether given event corresponds to a recorded digital input event
 * and send out VIDEO_EVENT_INFO message if it does.
 *
 * @param[in] event - Number of event
 * @param[in] triggerValue  - State of input
 *
 * @return None
 *
 */
static void check_digital_events(uint8_t event, uint8_t triggerValue)
{
    uint8_t              digitalInput           = 0;
    uint8_t              numberOfDigitalInputs;


    numberOfDigitalInputs = pConfig->configuration.numDigitalInputConfigurations;

    // Search through the digital events to find a match for the discrete event
    for (digitalInput = 0; digitalInput < numberOfDigitalInputs; digitalInput++)
    {
        if (pConfig->configuration.digitalInputConfiguration[digitalInput].number == event)
        {
            if (pConfig->configuration.digitalInputConfiguration[digitalInput].recording)
            {
                // Change of state should be recorded so send message to VRA
                video_rec_send_video_event_info(pConfig->configuration.digitalInputConfiguration[digitalInput].name,
                                                triggerValue);                    
            }
            break;
        }
    } // end digital input loop
} //check_digital_events

/**
 * Process discrete input signal
 *  This function is called from process_1sec_callback_event_control()
 *
 * @param[in] numEvents - number of discrete input events to process
 * @param[in] eventPtr  - pointer to array of state changed discrete inputs
 *
 * @return EXIT_SUCCESS
 *
 */
int process_discrete_input_data(uint8_t numEvents, uint8_t* eventPtr)
{
    uint8_t                cameraIndex       = 0;
    uint8_t         	   triggerValue      = 0;
    bool          		   matchFound        = false;
    uint16_t      		   index             = 0;
    uint8_t       		   eventIndex        = 0;              // loop through the input array
    uint8_t       		   digInputIndex     = 0;
    bool          		   digitalFound      = false;
    const uint8_t          bogusTriggerValue = 99;                                                   // never produce a match for invalid   
    
    sVSettingsDuringEvent *camEventsPtr      = NULL;                                                 // pointer for camera events
    uint8_t                numberOfCameras   = 0;

    numberOfCameras   = pConfig->configuration.numCameraConfigurations;

    
    for (eventIndex = 0; eventIndex < numEvents; eventIndex++)
    {
        // get the trigger value for the discrete event
        switch (*eventPtr)
        {
            case 1:
                triggerValue = discreteFpgaReg.bits.input_1;
                break;
            case 2:
                triggerValue = discreteFpgaReg.bits.input_2;
                break;
            case 3:
                triggerValue = discreteFpgaReg.bits.input_3;
                break;
            default:
                triggerValue = bogusTriggerValue;  // invalid
                break;
        }
#ifdef DEBUG
		syslog(LOG_DEBUG, "%s: Digital event trigger value %d", __func__, triggerValue);
#endif 
        // Search through the digital inputs to find a match for the discrete event
        check_digital_events(*eventPtr, triggerValue);

        // Search through the cameras to find a match for the discrete event
        for (cameraIndex = 0; cameraIndex < numberOfCameras; cameraIndex++)
        {
            // assign a pointer for code readability
            camEventsPtr = &pConfig->configuration.cameraConfiguration[cameraIndex].settingsDuringEvent;

            // check whether this camera is controlled by events
            if (camEventsPtr->cameraControlByEvent)
            {
                // initialize search variables
                matchFound = false;
                index      = 0;

                while ((matchFound == false) && (index < camEventsPtr->numEventsForCameraControl))
                {
                    if ((camEventsPtr->eventForControlledCamera[index].digitalInputNum == *eventPtr) &&
                        (camEventsPtr->eventForControlledCamera[index].eventInputState == triggerValue))
                    {
                        matchFound = true;

                        // initialize digital search variables
                        digInputIndex = 0;
                        digitalFound  = false;

                        // record the video event information immediately
                        while ((digInputIndex < pConfig->configuration.numDigitalInputConfigurations) &&
                               (digitalFound == false))
                        {
                            // the event number provides the discrete input name
                            if (camEventsPtr->eventForControlledCamera[index].digitalInputNum ==
                                    pConfig->configuration.digitalInputConfiguration[digInputIndex].number)
                            {
                                // found the input, send the message and break out of the loop
                                digitalFound = true;
                            }
                            // look at the next digital input
                            ++digInputIndex;
                        } // end digital input search loop
                        
                        // Save the event input pointer and camera configuration in the Timer Array to perform the action
                        discreteInputTimerArr[cameraIndex].eventInputPtr = &camEventsPtr->eventForControlledCamera[index];
                        discreteInputTimerArr[cameraIndex].camConfigPtr  = &pConfig->configuration.cameraConfiguration[cameraIndex];

                        // check for delayed action
                        if (camEventsPtr->eventForControlledCamera[index].actionDelayAfterEventTrigger > 0)
                        {
                            // check whether camera timer is already running
                            if (ptc_timer_active ((timer_id_t) discreteInputTimerArr[cameraIndex].timer_id) == TRUE)
                            {
                                // timer is running, convert to milliseconds and restart it
                                ptc_timer_stop((timer_id_t) discreteInputTimerArr[cameraIndex].timer_id);
                                ptc_timer_start((timer_id_t) discreteInputTimerArr[cameraIndex].timer_id,
                                                (camEventsPtr->eventForControlledCamera[index].actionDelayAfterEventTrigger * 1000),
                                                (camEventsPtr->eventForControlledCamera[index].actionDelayAfterEventTrigger * 1000));
                            }
                            else
                            {
                                // start the timer for the event action, convert to milliseconds
                                ptc_timer_start((timer_id_t) discreteInputTimerArr[cameraIndex].timer_id,
                                                (camEventsPtr->eventForControlledCamera[index].actionDelayAfterEventTrigger * 1000),
                                                (camEventsPtr->eventForControlledCamera[index].actionDelayAfterEventTrigger * 1000));

                            }
                        }
                        else
                        {
                            // immediate action required, see if an action is scheduled already
                            if (ptc_timer_active((timer_id_t) discreteInputTimerArr[cameraIndex].timer_id) == TRUE)
                            {
                                // need to stop the previous timer event for this camera
                                ptc_timer_stop((timer_id_t) discreteInputTimerArr[cameraIndex].timer_id);
                            }
                            // perform the action immediately
                            performDiscreteInputAction(&pConfig->configuration.cameraConfiguration[cameraIndex],
                                                       discreteInputTimerArr[cameraIndex].eventInputPtr, cameraIndex);
                        }                           
                    }
                    ++index;
                }
            } // end if camera controlled by event
        } // end camera loop
        // move to the next event
        ++eventPtr;
    } // end event loop   
    return (EXIT_SUCCESS);
} //process_discrete_input_data

/**
 *  This function is called when a Delayed Action Discrete Input Timer expires.
 *  It identifies the timer and camera configuration associated with the
 *  discrete event and calls another routine to perform the delayed action
 *
 * @param[in] tid - identifies the timer that expired
 * @param[in] xcount, *p_user - Required by ptc callback function but not used*
 *
 * @return None
 *
 */
static void discreteInputTimerEventHandler(timer_id_t tid, INT32 xcount, void *p_user)
{
    bool    timerEntryFound = false;
    uint8_t timerArrIndex   = 0;
    
    while ((timerArrIndex < MAX_NUMBER_OF_CAMERA_CONFIGURATIONS) && (!timerEntryFound))
    {
        // identify the event and its associated camera configuration
        if (tid == (timer_id_t) discreteInputTimerArr[timerArrIndex].timer_id)
        {
            // found now perform the delayed action
            timerEntryFound = true;
            // stop the timer
            ptc_timer_stop(tid);
			syslog(LOG_NOTICE, "discreteInputTimerEventHandler: Handler Timeout in event_control.c");
            performDiscreteInputAction(discreteInputTimerArr[timerArrIndex].camConfigPtr,
                                       discreteInputTimerArr[timerArrIndex].eventInputPtr, timerArrIndex);
        }
        else
        {
            // look at the next one
            ++timerArrIndex;
        }
    }
} //discreteInputTimerEventHandler

/**
 *  This function is called to perform an action on a camera when a discrete
 *  input event occurs. Currently it can Record, Pause or change a frame rate.
 *
 * @param[in] camConfigPtr - pointer to a camera configuration structure
 * @param[in] eventInputPtr - pointer to the camera event input parameter struct
 * @param[in] cameraConfigIndex - identifies the camera configuration
 *
 * @return None
 *
 */
void performDiscreteInputAction(sVCameraConfiguration           *camConfigPtr,
                                sVInputForEventControlledCamera *eventInputPtr,
                                uint8_t                          cameraConfigIndex)
{
    camera_record_pause_t cameraRecordPause;
    syslog(LOG_DEBUG, "%s:%s[%d]", __FILE__, __FUNCTION__, __LINE__);

    // Determine the action that needs to be taken with this camera
    switch (eventInputPtr->cameraFunctionForEvent)
    {
        case eVCameraFunctionForEventRecord:
        {
            #ifdef ENABLE_MULTI_SETTING_CHANGE_ON_EVENT
            // Not supporting this level of functionality for Release 1.
            // This will need to be reviewed when it comes time to support it.
            // There is currently no logic to support getting these settings
            // right when there are no explicit "during-event" settings in the
            // config and so there are only "during-config" settings.  Maybe
            // the message shouldn't be sent at all in that case.  Also,
            // sending these settings would need to be done consistently
            // across potentially all cases of cameraFunctionForEvent.

            // program the camera settings
            configuration_send_camera_settings(camConfigPtr->name,
                                               eventInputPtr->resolutionDuringEvent,
                                               eventInputPtr->compressionDuringEvent,
                                               eventInputPtr->frameRateDuringEvent,
                                               eventInputPtr->audioEnabledDuringEvent,
                                               camConfigPtr->settingsDuringConfiguration.essential,
                                               camConfigPtr->settingsDuringConfiguration.camera_max_recording_hours);
            // save the current frame rate for status reporting
            configurationSetCurrentFrameRate(cameraConfigIndex, 
                                             eventInputPtr->frameRateDuringEvent);
            #endif

            // now tell the camera to Record
            memset((char *) &cameraRecordPause, 0, sizeof(cameraRecordPause));
            strncpy(&cameraRecordPause.camera[0], camConfigPtr->name, CAMERA_NAME_LENGTH);
            cameraRecordPause.record = CAM_RECORD;
            configuration_send_camera_recording_ctrl_msg ((camera_record_pause_t *) &cameraRecordPause);
            syslog( LOG_NOTICE, "%s: Sending Camera RECORD message to VRA, Camera: %d.", __func__, camConfigPtr->id );

            /*update runtime camera status*/
            pVideoRecInfo->runTimeInfo.cameraStatus[cameraConfigIndex].cameraCommandedPause = FALSE; 
            break;
        }
        case eVCameraFunctionForEventPause:
        {
            // Tell the camera to Pause Recording
            memset((char *) &cameraRecordPause, 0, sizeof(cameraRecordPause));
            strncpy(&cameraRecordPause.camera[0], camConfigPtr->name, CAMERA_NAME_LENGTH);
            cameraRecordPause.record = CAM_PAUSE;
            configuration_send_camera_recording_ctrl_msg ((camera_record_pause_t *) &cameraRecordPause);
            syslog( LOG_NOTICE, "%s: Sending Camera PAUSE message to VRA, Camera: %d.", __func__, camConfigPtr->id );
            
            /*update runtime camera status*/
            pVideoRecInfo->runTimeInfo.cameraStatus[cameraConfigIndex].cameraCommandedPause = TRUE; 
            break;
        }
        case eVCameraFunctionForEventSetFrameRate:
        {
            // Change the frame rate for the camera
            video_rec_send_camera_frame_rate(camConfigPtr->name, 
                                             eventInputPtr->frameRateDuringEvent);
            // save the current frame rate for status reporting
            syslog( LOG_NOTICE, "%s: Sending Camera Frame Rate change message to VRA, Camera: %d.", __func__, camConfigPtr->id );
            configurationSetCurrentFrameRate(cameraConfigIndex, 
                                             eventInputPtr->frameRateDuringEvent);
            break;
        }
        default:
            break;
    }  // end switch on event function
} //performDiscreteInputAction

/**
 * Send camera new frame rate
 *
 * @param[in]
 *   char *     camera_name
 *   uint8_t    frame_rate
 *
 * @return EXIT_SUCCESS
 *
 */
int video_rec_send_camera_frame_rate (const char *camera_name,
                                      uint8_t     frame_rate)
{
    va_camera_frame_rate_t cameraFrameRateMsg;
    syslog(LOG_DEBUG, "%s:%s[%d]", __FILE__, __FUNCTION__, __LINE__);

    cameraFrameRateMsg.payload.header.version = CAMERA_FRAME_RATE_MSG_VERSION;

    if (camera_name != NULL)
    {
        memset(cameraFrameRateMsg.payload.camera, '\0', MAX_CAMERA_NAME_SIZE );
        memcpy(cameraFrameRateMsg.payload.camera, camera_name,MAX_CAMERA_NAME_SIZE );
    }
    cameraFrameRateMsg.payload.frame_rate = frame_rate;

    PTC_SEND_MSG_WITH_PAYLOAD(CAMERA_FRAME_RATE, cameraFrameRateMsg, va_camera_frame_rate_t);
#ifndef QNX_BUILD
	syslog(LOG_NOTICE, "%s: Send Camera Frame Rate Msg to VRA. Frame rate: %d, Camera Name: %.*s\n", __FUNCTION__,
		cameraFrameRateMsg.payload.frame_rate, sizeof(cameraFrameRateMsg.payload.camera), cameraFrameRateMsg.payload.camera);
#else
    syslog(LOG_NOTICE, "%s: Send Camera Frame Rate Msg to VRA. Frame rate: %d, Camera Name: %s\n", __FUNCTION__, 
           cameraFrameRateMsg.payload.frame_rate, cameraFrameRateMsg.payload.camera);
#endif	// #ifndef QNX_BUILD

    return (EXIT_SUCCESS);
} //video_rec_send_camera_frame_rate

/**
 * Send video event information
 *
 * @param[in] char *event_name    - name of the discrete input
 * @param[in] uint8_t event_state - state of the discrete input
 *                                  (HIGH/LOW)
 *
 * @return EXIT_SUCCESS
 *
 */
int video_rec_send_video_event_info(const char * event_name, uint8_t event_state)
{
    va_video_event_info_t  eventInfoMsg;
    syslog(LOG_DEBUG, "%s:%s[%d]", __FILE__, __FUNCTION__, __LINE__);
    
    // put the version in the header
    eventInfoMsg.payload.header.version = VIDEO_EVENT_INFO_MSG_VERSION;

    // fill in the event name
    if (event_name != NULL)
    {
        memset(eventInfoMsg.payload.name, '\0', EVENT_NAME_LENGTH);
        memcpy(eventInfoMsg.payload.name, event_name, EVENT_NAME_LENGTH);
    }
    // fill in the discrete input state (HIGH/LOW)
    eventInfoMsg.payload.value = event_state;

    // send the message
    PTC_SEND_MSG_WITH_PAYLOAD(VIDEO_EVENT_INFO, eventInfoMsg, va_video_event_info_t);

#ifndef QNX_BUILD
	syslog(LOG_NOTICE, "%s: Send Video Event Info to VRA. Event state: %d, Event Name: %.*s\n", __FUNCTION__,
		eventInfoMsg.payload.value, sizeof(eventInfoMsg.payload.name), eventInfoMsg.payload.name);
#else
    syslog(LOG_NOTICE, "%s: Send Video Event Info to VRA. Event state: %d, Event Name: %s\n", __FUNCTION__, 
           eventInfoMsg.payload.value, eventInfoMsg.payload.name);
#endif	// #ifndef QNX_BUILD

    return (EXIT_SUCCESS);
    
} //video_rec_send_video_event_info

/**
 * This function monitors the CHM mount status and performs mount/unmount 
 * based on the chn_connect from CHM Data recording and video recording 
 * status
 *
 * @return None
 *
 */
#ifndef QNX_BUILD
void process_chm_connect_status(void)
{
    static int chm_ready = -1 ;
    static int check_counter = CHECK_CHM_CONNECT_THRESHOLD ; 
    int chm_connect = 0 ; 
    pthread_t chm_mount_thread; 
    

    /* only proceed if this reaches the threshold to avoid accessing shared memory too often */
    if (check_counter++ < CHECK_CHM_CONNECT_THRESHOLD) {
        return ;
    }

    check_counter = 0;   

    if (videoRec_GetCHMConenctSharedMem(&chm_connect) == EXIT_SUCCESS)
    {        
        if (chm_connect == 1) {
            if (filesys_is_mounted(CHM_VIDEO_MOUNT) == FALSE) {
                pthread_create(&chm_mount_thread, NULL, chmSSDMountHandler, NULL);
            }

            if (filesys_is_mounted(CHM_VIDEO_MOUNT) == TRUE) {
                pVideoRecInfo->chmReadyFlag = READY ; 
            }            
        }
        else if ((chm_connect == 0) && (pVideoRecInfo->recStatusOnCHM != TRUE)) { 
            if (filesys_is_mounted(CHM_VIDEO_MOUNT) == TRUE) {
                umount2(CHM_VIDEO_MOUNT,MNT_DETACH);
            }

            if (filesys_is_mounted(CHM_VIDEO_MOUNT) == FALSE) {
                pVideoRecInfo->chmReadyFlag = NOT_READY ; 
            }
        }

        if (pVideoRecInfo->chmReadyFlag != chm_ready) {
            if (chm_ready != -1) {
                ready_status_t              ready_status;
                // Send the Ready Status Message to VRA the CHM is known to be ready
                memset( (char *) &ready_status, 0, sizeof(ready_status));
                ready_status.chm_ready           = (pVideoRecInfo->chmReadyFlag == READY);
                ready_status.removable_ssd_ready = (pVideoRecInfo->removableReady == READY);
                // save drive ready status and send out when camera are ready
                pVideoRecInfo->driveReadyStatus     = ready_status;
                pVideoRecInfo->driveStatusRequested = TRUE; 

                syslog(LOG_NOTICE, "request drive status update ");
            }
            chm_ready = pVideoRecInfo->chmReadyFlag; 
        }
    }    
}

/**
 *  This function monitors the door lock of the removable drive slot. A change in the status
 *  will trigger a message to the video application and the mounting/unmounting of the drive.
 *
 * @return None
 *
 */
#ifndef LNVR
void process_door_lock_status(void)
{
    static door_lock_state prev_lock_status = DOOR_UNKNOWN;
    door_lock_state        curr_lock_status = DOOR_UNKNOWN;
    
    shutdown_t             shutdown;

    curr_lock_status = getDoorLockStatus();

    if ( (prev_lock_status == DOOR_UNLOCKED) && (curr_lock_status == DOOR_LOCKED) )
    {
        // Avoid checking whether removable drive is available during the time
        // when the OS is still releasing the resources it allocated to access
        // the drive.  This can happen when the user actually removes the drive
        // then shuts the caddy door and locks it shortly afterwards.
        syslog(LOG_NOTICE, "%s: Caddy door locked after being unlocked. Force a mount check.", __func__);
        checkMount = true;
    }

    // Update lock status
    prev_lock_status = curr_lock_status;

    if (curr_lock_status == DOOR_UNLOCKED)
    {
        // if removable SSD is in operation
        if (pVideoRecInfo->removableReady == READY)
        {
            pVideoRecInfo->shutdownStarted = TRUE;  // set flag to indicate shutdown process has started.

            // If the shutdown timer has stopped
            if (ptc_timer_active(pVideoRecInfo->videoShutdownTimer) == FALSE)
            {
                // send removable shutdown command to REAGENT VR
                memset( (char *) &shutdown, 0, sizeof(shutdown));
                shutdown.function_type = SHUTDOWN_REMOVABLE_SSD;
                send_reagent_vr_shutdown_command((shutdown_t *) &shutdown);

                syslog(LOG_NOTICE, "%s: Caddy door unlocked. Sent SHUTDOWN_REMOVABLE_SSD command to VRA.", __func__);

                // need to sleep for minimum  of 5 secs before umounting removable drive
                // sleep time is necessary to give omeVRA time to release the drive
                // anything less than 5 secs will return an error from the OS when unmount
                // An issue has been generated for REAGENT to reduce the time to 3 secs or less
                // start the timer for the event action, convert to milliseconds
                ptc_timer_start((timer_id_t) pVideoRecInfo->videoShutdownTimer, VIDEO_SHUTDOWN_TIMEOUT, VIDEO_SHUTDOWN_TIMEOUT);
            }
        }
        else
        {
            if (startup == true)
            {
                // The unit may have started with the caddy door open. Send the Ready Status Message.
                syslog(LOG_NOTICE, "%s: Caddy door is unlocked at startup.", __func__); 
                // Send the Ready Status Message to VRA the CHM is known to be ready
                // or else the VRA handshaking would never have started
                // This will restart the recording to the removable
                memset( (char *) &ready_status, 0, sizeof(ready_status));
                ready_status.chm_ready           = (pVideoRecInfo->chmReadyFlag == READY);                                    
                ready_status.removable_ssd_ready = (pVideoRecInfo->removableReady == READY);
                // save drive ready status and send out when camera are ready
                pVideoRecInfo->driveReadyStatus     = ready_status;
                pVideoRecInfo->driveStatusRequested = TRUE;            
            }
        }
    }
    else if (curr_lock_status == DOOR_LOCKED)
    {
        // Door is Locked
        if (startup == true)
        {
            syslog(LOG_NOTICE, "%s: Caddy door is locked at startup.", __func__);       
        }
        
        if (   (pVideoRecInfo->removableReady == NOT_READY)
            &&  checkMount
            && (ptc_timer_active(pVideoRecInfo->mountRemovableSSDTimer) == FALSE)
            && (pVideoRecInfo->shutdownStarted == FALSE))
        {
            syslog(LOG_NOTICE, "%s: Caddy door locked. Start mount timer and mount removable SSD.", __func__);      
            ptc_timer_start((timer_id_t) pVideoRecInfo->mountRemovableSSDTimer, REMOVABLE_SSD_MOUNT_TIMEOUT, REMOVABLE_SSD_MOUNT_TIMEOUT);

            startRemovableSSDMountProcess();

            // No need to keep checking whether drive is available once it has
            // been determined that it is not available
            checkMount = false;
        }
    } //if (curr_lock_status == DOOR_LOCKED)
    else
    {
        syslog(LOG_ERR, "%s: Invalid door lock status %d", __FUNCTION__, curr_lock_status);
    } //else

    // Reset flag.
    startup = false;
} //process_door_lock_status

/**
 * Event Handler for the mount removable SSD Timer
 *
 * @param[in] tid     - timer id
 * @param[in] xcount  - count
 * @param[in] *p_user - NULL pointer
 *
 * @return EXIT_SUCCESS
 *
 */
static void mountRemovableSSDTimerEventHandler(timer_id_t tid, INT32 xcount, void *p_user)
{
	char dir[256] = {'\0'};
    ready_status_t  ready_status;
    door_lock_state lock_status   = DOOR_UNKNOWN;

    syslog(LOG_NOTICE, "%s: Mount timer expired. Check removable SSD mount status.", __func__);

    // Do not need to check mount or send a ready status message if a shutdown process
    // has already started.
    if (filesys_is_mounted(REMSSD_VIDEO_MOUNT) == FALSE)
    {
        // somehow mount has failed.
        pVideoRecInfo->removableReady = NOT_READY;

        // Now turn off power to removable SSD.
        removableSDDPowerOperation(REMOVABLE_SSD_POWER_OFF);
        syslog(LOG_NOTICE, "%s: Mount timer expired. Removable SSD mount failure.Turn removable SSD power off.", __func__);
    }
    else
    {
        syslog(LOG_NOTICE, "%s: Mount timer expired. Filesystem is mounted on rSSD.", __func__);
        pVideoRecInfo->removableReady = READY;

		// Check if the Video Footages directory is created. If not create it.
		if ((create_new_directory((char *) REMSSD_VIDEO_MOUNT, (char *) FOOTAGES_DIR)) == 0)
		{
			strcat(dir, REMSSD_VIDEO_MOUNT);
			strcat(dir, "/");
			strcat(dir, FOOTAGES_DIR);

			change_ownership ((const char *) dir, (const char *) REAGENT_USER, (const char *) REAGENT_GRP);
		}

		// Check if the Video Download directory is created. If not create it. Note this directory is used by both the
		// client video download as well as the URL download.
		if ((create_new_directory((char *) REMSSD_DOWNLOAD_MOUNT, (char *) VD_DOWNLOAD_DIR)) == 0)
		{
			memset((char *) dir, '\0', sizeof(dir));
			strcat(dir, REMSSD_DOWNLOAD_MOUNT);
			//strcat(dir, "/");
			strcat(dir, VD_DOWNLOAD_DIR);

			change_ownership ((const char *) dir, (const char *) REAGENT_USER, (const char *) REAGENT_GRP);
		}

        // copy the removable drive serial number
        get_removable_serial_number(pVideoRecInfo->rem_ssd_serial_number);
        syslog( LOG_NOTICE, "%s: LDRS-V: Removable SSD serial number %s", __func__,  pVideoRecInfo->rem_ssd_serial_number );
    }
    
    // Send the Ready Status Message. No need to send Ready Status Message if the
    // door is unlocked.
    lock_status = getDoorLockStatus();

    if (lock_status == DOOR_LOCKED)
    {
        memset( (char *) &ready_status, 0, sizeof(ready_status));

        ready_status.chm_ready           = ( pVideoRecInfo->chmReadyFlag == READY );
        ready_status.removable_ssd_ready = ( pVideoRecInfo->removableReady == READY );

        // save drive ready status and send out when camera are ready
        pVideoRecInfo->driveReadyStatus     = ready_status;
        pVideoRecInfo->driveStatusRequested = TRUE;
    }
    // Stop timer.
    ptc_timer_stop((timer_id_t) pVideoRecInfo->mountRemovableSSDTimer); 
} //mountRemovableSSDTimerEventHandler

/**
 *  This function is called when a holdoff mount timer expires. Stops the timer.
 *
 * @param[in] tid - identifies the timer that expired
 * @param[in] xcount, *p_user - Required by ptc callback function but not used*
 *
 * @return None
 *
 */
static void holdOffMountTimerEventHandler(timer_id_t tid, INT32 xcount, void *p_user)
{
    // Timer expired. For ptc timers need to explicitly stop the timer, otherwise resets itself.
#ifdef DEBUG
    syslog(LOG_DEBUG, "%s: Hold off mount timer expired", __FUNCTION__);
#endif
    ptc_timer_stop((timer_id_t) pVideoRecInfo->holdoffMountTimer);
    return;

} //holdOffMountTimerEventHandler
#endif


/**
 *  This function is called when a timer for monitoring updates for GPS data expires
 *
 * @param[in] tid - identifies the timer that expired
 * @param[in] xcount, *p_user - Required by ptc callback function but not used*
 *
 * @return None
 *
 */
void monitorGPSDataUpdateinSHMTimerEventHandler(timer_id_t tid, INT32 xcount, void *p_user)
{
    sSharedMemData        sharedMemAddr;
    sSharedMemData       *pSharedMemAddr            = &sharedMemAddr;
    sSysTimeMgrSharedMem *pSysTimeMgrSharedMem      = NULL;
    BOOL                  update_flag               = FALSE;
    char                  speed[GPS_SPEED_LENGTH+1] = {0};

    // Get shared memory data.
    if ( videoRec_GetGPSDataFromSharedMem(pSharedMemAddr, &update_flag) == EXIT_FAILURE)
    {
        syslog(LOG_ERR, "%s: Unable to get data from shared memory.", __FUNCTION__);
    }

    if (update_flag == TRUE)
    {
        pSysTimeMgrSharedMem = &pSharedMemAddr->sysTimeMgr;

        if (pSysTimeMgrSharedMem != NULL)
        {
            sprintf(speed, "%03d.%01d", pSysTimeMgrSharedMem->gprmcMessage.speedOverGroundKnots,
                    pSysTimeMgrSharedMem->gprmcMessage.speedOverGroundKnotsFrac);
            // systimemgr data has been updated in shared memory. Send message to VRA.
            video_rec_send_gps_data((const char *) pSysTimeMgrSharedMem->gprmcMessage.latitude,
                                    (const char *) pSysTimeMgrSharedMem->gprmcMessage.longitude,
                                    (char *) speed);
        }
    }
} //monitorGPSDataUpdateinSHMTimerEventHandler
#endif	// #ifndef QNX_BUILD

/**
 * Send GPS data information
 *
 * @param[in]  char *latitude  - array containing latitude data
 * @param[in]  char *latitude  - array containing longitude data
 *
 * @return     EXIT_SUCCESS
 *
 */
int video_rec_send_gps_data(const char *latitude, const char * longitude, const char *speed )
{
#ifdef LNVR
    va_gps_data_ver_1_t gpsData;
    int GPS_MSG_VER = GPS_DATA_MSG_VERSION_1;
#else
    va_gps_data_t gpsData;
    int GPS_MSG_VER = GPS_DATA_MSG_VERSION;
#endif
    memset((char *) &gpsData, 0, sizeof(gpsData));

    // put the version in the header
    gpsData.payload.header.version = GPS_MSG_VER;

    // Fill in the latitude and longitude information.
    if (latitude != NULL)
    {
        memcpy(gpsData.payload.latitude, latitude, GPS_LATITUDE_LENGTH);
    }

    if (longitude != NULL)
    {
        memcpy(gpsData.payload.longitude, longitude, GPS_LONGITUDE_LENGTH);
    }
#ifdef LNVR
    // send the message
    PTC_SEND_MSG_WITH_PAYLOAD(GPS_DATA, gpsData, gps_data_ver_1_t);
#ifdef DEBUG
        syslog(LOG_NOTICE, "%s: Sent GPS data to VRA. latitude: %.*s, Longitude: %.*s", 
               __FUNCTION__, GPS_LATITUDE_LENGTH,  gpsData.payload.latitude, 
                             GPS_LONGITUDE_LENGTH, gpsData.payload.longitude);
#endif
#else
    if (speed != NULL)
    {
        memcpy(gpsData.payload.speed, speed, GPS_SPEED_LENGTH);
    }
    // send the message
    PTC_SEND_MSG_WITH_PAYLOAD(GPS_DATA, gpsData, va_gps_data_t);
#ifdef DEBUG
        syslog(LOG_NOTICE, "%s: Sent GPS data to VRA. latitude: %.*s, Longitude: %.*s, Speed: %.*s\n", 
               __FUNCTION__, GPS_LATITUDE_LENGTH,  gpsData.payload.latitude, 
                             GPS_LONGITUDE_LENGTH, gpsData.payload.longitude, 
                             GPS_SPEED_LENGTH,     gpsData.payload.speed);
#endif
#endif
    return (EXIT_SUCCESS);
} //video_rec_send_gps_data


#ifndef QNX_BUILD
/**
 * chmSSDMountHandler to mount the CHM SSD
 *
 * @param[in] ptr - NOT IN USE
 *
 * @return N/A
 *
 */
void *chmSSDMountHandler(void *ptr)
{
    struct usr_def_fstab  SSDtable;    
    
    /*
     * check the presence of the CHM 
     */
    memset( &SSDtable, '\0', sizeof(struct usr_def_fstab));

    if (getDriveFsInfo( &SSDtable, CHM_VIDEO ) == true)
    {
        // found information of the drive, now use it to look if the drive is present or not
        if (access(SSDtable.symlinkfullpathname, F_OK) == 0)  // 'F_OK' flag means test for existence of the file
        {
            mount(SSDtable.devicepathname, SSDtable.mountpoint, SSDtable.fstype, MS_MGC_VAL | MS_SYNCHRONOUS, "") ;                 
        }      
    }
    return NULL;
}

/**
 * Thread handler for the removable SSD
 *
 * @param[in] info - pointer to door lock info
 *
 * @return NULL
 *
 */
#ifndef LNVR
void *removableSSDThreadhandler(void *info)
{
    door_lock_state lock_status = DOOR_UNKNOWN;

    syslog(LOG_NOTICE, "%s: Executing thread to mount removable SSD partitions", __FUNCTION__);

    // Turn on power to removable SSD
    removableSDDPowerOperation(REMOVABLE_SSD_POWER_ON);
    sleep(10); 

    // This thread was started to mount the removable SSD when the caddy door was detected to be locked.
    // Is it still locked at this time?
    lock_status = getDoorLockStatus();

    if (lock_status == DOOR_LOCKED)
    {
        // Attempt to mount the partitions in the removable SSD
        syslog(LOG_NOTICE, "%s: Caddy door locked. Mount removable SSD partitions.", __func__);
        mount_removable_partitions();
    }
    else if (lock_status == DOOR_UNLOCKED)
    {
        // The door is unlocked now. Abort the mount and turn off power to the removable SSD.
        // No need for a delay since filesystems are not mounted.
        removableSDDPowerOperation(REMOVABLE_SSD_POWER_OFF);
        syslog(LOG_NOTICE, "%s: Caddy door unlocked after being locked. Turn removable SSD power off.", __func__);
    }

    // Sleep for a while for everything to stabilize
    sleep(10); 
    pVideoRecInfo->mount_proccess_complete = TRUE;
    syslog(LOG_NOTICE, "%s: Terminating thread to mount removable SSD partitions", __FUNCTION__);

    return (NULL);   
} //removableSSDThreadhandler

/**
 * Function to begin mounting the removable SSD
 *
 * @param[in] None
 *
 * @return    None
 *
 */
void startRemovableSSDMountProcess(void)
{
    int err = pthread_create(&rSSDThreadID, NULL, removableSSDThreadhandler, NULL);

    syslog(LOG_NOTICE, "%s: Create thread to mount removable SSD partitions", __FUNCTION__);

    if (err != 0)
    {
        syslog(LOG_ERR, "%s: pthread_create failed - %s \n", __FUNCTION__, strerror(err));
    }

    return;

} //startRemovableSSDMountProcess
#endif

/**
 * Function to check NAS events
 *
 * @param[in] action
 *
 * @return    None
 *
 */
void process_nas_event(uint32_t action)
{
    ready_status_t ready_status;
    shutdown_t     shutdown;

#ifdef DEBUG
    syslog(LOG_NOTICE, "%s: g_NasConfigured: %s", __func__, g_NasConfigured ? "YES" : "NO");
#endif

    if (!g_NasConfigured) return;  // should never get here

    if (action == SHUTDOWN && pVideoRecInfo->vraShutdownInProgress == FALSE)
    {
        shutdown.header.version = SHUTDOWN_MSG_VERSION;
        shutdown.function_type  = SHUTDOWN_REMOVABLE_SSD; 
        send_reagent_vr_shutdown_command((shutdown_t *) &shutdown);

        syslog(LOG_ERR, "%s: SHUTDOWN is requested. Sent SHUTDOWN_REMOVABLE_SSD command to VRA.", __func__);

        // need to sleep for minimum  of 5 secs before umounting removable drive
        // sleep time is necessary to give omeVRA time to release the drive
        // anything less than 5 secs will return an error from the OS when unmount
        // An issue has been generated for REAGENT to reduce the time to 3 secs or less
        // start the timer for the event action, convert to milliseconds
        ptc_timer_start((timer_id_t) pVideoRecInfo->videoShutdownTimer, VIDEO_SHUTDOWN_TIMEOUT, VIDEO_SHUTDOWN_TIMEOUT);
    }
    else if (action == READY_STATUS && pVideoRecInfo->vraShutdownInProgress == FALSE)
    {
        syslog(LOG_ERR, "%s[%d]: Ready request. Sent READY command to VRA.", __func__, __LINE__);

        // Send the Ready Status Message to VRA the CHM is known to be ready
        // or else the VRA handshaking would never have started
        // This will restart the recording to the removable
        memset( (char *) &ready_status, 0, sizeof(ready_status));
        ready_status.header.version = READY_STATUS_MSG_VERSION;  
        ready_status.chm_ready           = TRUE;   
        ready_status.removable_ssd_ready = TRUE;

        send_readystatus_vra((ready_status_t *) &ready_status);
        // send camera record / pause
        send_record_for_all_cameras();               
    }
    else if (action == RESTART_API)
    {
        //if previosuly video rec got shutdown, restart it for a clean start.
        // kill omeVRA
        syslog(LOG_NOTICE, "%s[%d] LNVR NAS REQUEST: to start video recording - previosuly shutdowned - restart video rec for clean start", __FUNCTION__, __LINE__);
        system("/opt/reagent/bin/ldrs_v_sim SHUTDOWN 2");
        usleep(TWO_POINT_FIVE_SECONDS_IN_USEC);              //giving Reagent time to shutdown. 
        exit(1);
    }
} //process_nas_event

/**
 * Function to begin mounting internal drive as removable SSD for LNVR
 *
 * @param[in] None
 *
 * @return    None
 *
 */
BOOL check_for_nas_and_mount(void)
{
    char                  cmd[MOUNT_POINT_MAX_LEN];
    int                   result                    = -1;

    if ((pConfig != NULL))
    {
        if (pConfig->configuration.numNasServerConfiguration <= 0)
        {
            if (is_mounted(REMSSD_VIDEO_MOUNT))
            {
                syslog(LOG_NOTICE, "%s already mounted as %s\n", REMSSD_VIDEO_PARTITION, REMSSD_VIDEO_MOUNT);
                checkMount = TRUE;
                return (TRUE);
            }
            syslog(LOG_NOTICE,"%s[%d]: LNVR_NAS is not configured,  Mounting internal SSD as Removable.",__FUNCTION__,__LINE__);
            memset(cmd, 0, MOUNT_POINT_MAX_LEN);
            sprintf(cmd, "%s %s %s", CMD_MNT_BYLABEL, REMSSD_VIDEO_PARTITION, REMSSD_VIDEO_MOUNT);
            result = system(cmd);

            if (result == 0)
            {
                syslog(LOG_NOTICE, "%s\n", cmd);
                checkMount = TRUE;               
            }
            else
            {
                syslog(LOG_ERR, "FAILED: %s\n", cmd);
            } 
        }
        else
        {
            syslog(LOG_ERR, "NAS is configured can't mount internal drive as remSSD");
        }
    }
    return (checkMount);
} //check_for_nas_and_mount
#endif	// #ifndef QNX_BUILD
