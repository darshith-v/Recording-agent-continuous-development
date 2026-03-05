/**
 * @file configuration.c
 *
 * @author Michael Nguyen
 *
 * @section DESCRIPTION
 * 
 * Manages video event control
 *
 * @section COPYRIGHT
 *
 * Copyright 2016-2020 WABTEC Railway Electronics
 * This program is the property of WRE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>          // system error code
#include <sys/io.h>         // for inb and outb (reading FPGA)
#include <syslog.h>

#include "arg_utils.h"      // For arg_opt_*() calls
#include "crc_utils.h"      // For crc32_*() calls
#include "exit_codes.h"     // For EXIT_PTC_* codes
#include "logger.h"         // For LOGF() calls

#include "ptc_api.h"        // For ptc_*() calls
#include "video_api.h"      // For message structure definitions
#include "video_rec.h"      // Function prototypes.

#include "ldars_sys.h"
#include "ldars_configuration.h"

#ifndef QNX_BUILD
#include "shmem_utils.h"
#endif	//#ifndef QNX_BUILD

// function prototypes
int configuration_send_all_camera_none_event_settings(void);

#ifdef QNX_BUILD
#define HARDCODED_CONFIGURATION
void updateConfiguration(sLDARSConfiguration *configuration);
eConfigurationType getConfiguration(sLDARSConfiguration *configuration);
#endif // #ifdef QNX_BUILD

/**
 * Initialize configuration module
 *
 * @param[in] None
 *
 * @return None
 *
 */
int init_configuration(void)
{
    int retStatus = EXIT_FAILURE;

	sLDARSConfiguration *pConfig = NULL;

    // get LDRS configuration from shared memory
	retStatus = getLocallyStoredConfiguration(&pConfig);

	if ((pConfig == NULL) || (retStatus == EXIT_FAILURE))
    {
		// Try to retrieve the configuration again.
		updateConfigurationFromSharedMemory();
    }

    return (EXIT_SUCCESS);
} //init_configuration

/*
 * Start configuration support.
 * Call this function upon receipt of Video Application Status message
 * 
 * This function is called from startup_modules() in main.
 *
 * @param[in] BOOL config_info_all
 *
 * @return    EXIT_SUCCESS - on Success
 *            EXIT_FAILURE - on Failure
 *
 */
int start_configuration(BOOL config_info_avail)
{
    int                  retStatus       = EXIT_FAILURE;
	sLDARSConfiguration *pConfig = NULL;

    // get LDRS configuration from shared memory
	retStatus = getLocallyStoredConfiguration(&pConfig);

	if ((pConfig != NULL) && (retStatus == EXIT_SUCCESS))
    {
		// send configuration settings to all cameras
		syslog(LOG_DEBUG, "%s: configuration_send_all_camera_none_event_settings: Send camera config for all cameras.", __func__);
		configuration_send_all_camera_none_event_settings();

		// save the current camera status for the UDP Status message
		updateConfiguration(pConfig);
		return (EXIT_SUCCESS);
	}
	else
	{
		syslog(LOG_CRIT,"%s: Invalid configuration. Msg not sent.", __func__);
    	return (EXIT_FAILURE);
	}
} //start_configuration

/**
 * Send all cameras Non-Event (Normal) Configuration Settings
 *
 * @param[in] void
 *
 * @return EXIT_SUCCESS - on Success
 *         EXIT_FAILURE - on Failure
 *
 */
int configuration_send_all_camera_none_event_settings(void)
{
    uint8_t                cameraIndex     = 0;
    uint8_t                numberOfCameras = 0;
    int                    retStatus       = EXIT_FAILURE;
	sLDARSConfiguration   *pConfig         = NULL;
	va_init_camera_list_t  initCameraList;

	// Start with clean structures
	memset(&initCameraList, 0, sizeof(initCameraList));

    syslog(LOG_DEBUG, "%s[%d]  ", __FUNCTION__, __LINE__);

	retStatus = getLocallyStoredConfiguration(&pConfig);

	if ((pConfig != NULL) && (retStatus == EXIT_SUCCESS))
	{
    	numberOfCameras = pConfig->configuration.numCameraConfigurations;
    	syslog(LOG_NOTICE,"%s: Num cameras : %d", __FUNCTION__, numberOfCameras);

	    for (cameraIndex = 0; cameraIndex < numberOfCameras ; cameraIndex++)
	    {
			// Populate the Initial Camera List
			strncpy(initCameraList.payload.camera_list[cameraIndex].camera, pConfig->configuration.cameraConfiguration[cameraIndex].name, CAMERA_NAME_LENGTH);
			initCameraList.payload.camera_list[cameraIndex].resolution   = pConfig->configuration.cameraConfiguration[cameraIndex].settingsDuringConfiguration.resolutionDuringConfig;
			initCameraList.payload.camera_list[cameraIndex].compression  = pConfig->configuration.cameraConfiguration[cameraIndex].settingsDuringConfiguration.compressionDuringConfig;
			initCameraList.payload.camera_list[cameraIndex].frame_rate   = pConfig->configuration.cameraConfiguration[cameraIndex].settingsDuringConfiguration.frameRateDuringConfig;
			initCameraList.payload.camera_list[cameraIndex].audio_enable = pConfig->configuration.cameraConfiguration[cameraIndex].settingsDuringConfiguration.audioEnabledDuringConfig;
			initCameraList.payload.camera_list[cameraIndex].is_essential = pConfig->configuration.cameraConfiguration[cameraIndex].settingsDuringConfiguration.essential;
			initCameraList.payload.camera_list[cameraIndex].camera_max_recording_hours = pConfig->configuration.cameraConfiguration[cameraIndex].settingsDuringConfiguration.camera_max_recording_hours;
			
			// The setting that can be modified is the frame rate by event control
	        // save the current frame rate for status reporting
	        configurationSetCurrentFrameRate(cameraIndex, 
						                     pConfig->configuration.cameraConfiguration[cameraIndex].settingsDuringConfiguration.frameRateDuringConfig);

    		syslog(LOG_NOTICE,"%s: Initial Camera List : camera %s, resolution: %d, compression: %d, frame_rate: %d, no_audio_recording: %d, no_chm_recording: %d, max_age: %d\n", 
			__FUNCTION__,  initCameraList.payload.camera_list[cameraIndex].camera, initCameraList.payload.camera_list[cameraIndex].resolution, 
				initCameraList.payload.camera_list[cameraIndex].compression,
				initCameraList.payload.camera_list[cameraIndex].frame_rate,
				initCameraList.payload.camera_list[cameraIndex].audio_enable,
				initCameraList.payload.camera_list[cameraIndex].is_essential,
				initCameraList.payload.camera_list[cameraIndex].camera_max_recording_hours);

	    }

	    initCameraList.payload.header.version = INIT_CAMERA_LIST_MSG_VERSION ;
	    // Send the Camera List to Recording Agent
		PTC_SEND_MSG_WITH_PAYLOAD(INIT_CAMERA_LIST, initCameraList, va_init_camera_list_t);
		syslog(LOG_NOTICE,"%s: Initial Camera List sent to Recording Agent.", __FUNCTION__);
	}
	else
	{
		syslog(LOG_CRIT,"%s: Invalid configuration. Msg not sent.", __func__);
    	return (EXIT_FAILURE);
    }
    return (EXIT_SUCCESS);
} //configuration_send_all_camera_none_event_settings

/**
 * Send camera configuration settings via PTC using va_camera_settings_t message type
 *
 * @param[in]
 *      char *  camera_name
 *      uint8_t resolution
 *      uint8_t compression
 *      uint8_t frame_rate
 *      BOOL    no_audio_recording
 *      BOOL    no_chm_recording
 *
 * @return EXIT_SUCCESS
 *
 */
int configuration_send_camera_settings (char * camera_name, uint8_t resolution, uint8_t compression,
                                        uint8_t frame_rate, BOOL no_audio_recording, BOOL no_chm_recording, uint8_t max_age)
{
    va_camera_settings_t cameraSettingMsg;

    cameraSettingMsg.payload.header.version = CAMERA_SETTINGS_MSG_VERSION;

    if (camera_name != NULL)
    {
		// strncpy removes the need for memset - the next memcpy over-wrote all of camera with whatevr was in camera_name
        //memset(cameraSettingMsg.payload.camera, '\0', MAX_CAMERA_NAME_SIZE );
        strncpy(cameraSettingMsg.payload.camera, camera_name, MAX_CAMERA_NAME_SIZE );
    }

    cameraSettingMsg.payload.resolution         = resolution;
    cameraSettingMsg.payload.compression        = compression;
    cameraSettingMsg.payload.frame_rate         = frame_rate;
    cameraSettingMsg.payload.no_audio_recording = (!no_audio_recording); // Reverse logic here
    cameraSettingMsg.payload.no_chm_recording   = (!no_chm_recording);	 //Reverse logic here
	cameraSettingMsg.payload.grooming_duration = max_age;

    PTC_SEND_MSG_WITH_PAYLOAD(CAMERA_SETTINGS, cameraSettingMsg, va_camera_settings_t);
    syslog(LOG_NOTICE,"%s: Camera Settings sent to VRA: camera %s, resolution: %d, compression: %d, frame_rate: %d, no_audio_recording: %d, no_chm_recording: %d, max_age: %d\n", 
		__FUNCTION__, cameraSettingMsg.payload.camera, cameraSettingMsg.payload.resolution, cameraSettingMsg.payload.compression, 
			cameraSettingMsg.payload.frame_rate, cameraSettingMsg.payload.no_audio_recording, cameraSettingMsg.payload.no_chm_recording, cameraSettingMsg.payload.grooming_duration);

    return (EXIT_SUCCESS);
} //configuration_send_camera_settings


/**
 * Saves the current frame rate setting of a given camera for use in status
 * reporting
 *
 * @param[in] cameraIndex - identifies the camera
 * @param[in] frameRame   - current frame rate setting 
 *
 */
void configurationSetCurrentFrameRate(uint8_t cameraIndex, uint8_t frameRate)
{
	sLDARSConfiguration *pConfig = NULL;
	uint8_t              rc      = EXIT_FAILURE;

	rc = getLocallyStoredConfiguration(&pConfig);

	if ((pConfig != NULL) && (rc == EXIT_SUCCESS))
	{
		pConfig->configuration.cameraConfiguration[cameraIndex].settingsCurrent.cameraFrameRate = frameRate;
	}
	else
	{
		syslog(LOG_CRIT,"%s: Invalid configuration. Frame rate not set.", __func__);
	}
} //configurationSetCurrentFrameRate

/**
 * Send Digital Input Configuration via PTC 
 * using va_digital_inputs_t message type
 *
 * @param[in] void
 *
 * @return EXIT_SUCCESS
 *
 */
int configuration_send_digital_inputs(void)
{
    uint8_t              rc            = EXIT_FAILURE;
	uint8_t              numDigInputs  = 0;
	uint8_t              digInputIndex = 0;
	va_digital_inputs_t  digitalInputs;
	sLDARSConfiguration *pConfig       = NULL;

	// Start with clean structures
	memset(&digitalInputs.payload , 0, sizeof(digitalInputs.payload));
    digitalInputs.payload.header.version = DIGITAL_INPUTS_MSG_VERSION ;

	// Get system configuration to populate the local data struct.
	rc = getLocallyStoredConfiguration(&pConfig);

	if ((pConfig != NULL) && (rc == EXIT_SUCCESS))
	{
		numDigInputs = pConfig->configuration.numDigitalInputConfigurations;
		// If Digital Inputs are configured, populate the data structure and send the API message to Recording Agent
		if (numDigInputs > 0)
		{
			for (digInputIndex = 0; digInputIndex < numDigInputs; digInputIndex++)
			{
				strncpy(digitalInputs.payload.di[digInputIndex].di_name, pConfig->configuration.digitalInputConfiguration[digInputIndex].name, EVENT_NAME_LENGTH);
				digitalInputs.payload.di[digInputIndex].di_number    = pConfig->configuration.digitalInputConfiguration[digInputIndex].number;
				digitalInputs.payload.di[digInputIndex].di_recording = pConfig->configuration.digitalInputConfiguration[digInputIndex].recording;
			}

			// Send the Digital Inputs Configuration to Recording Agent
			PTC_SEND_MSG_WITH_PAYLOAD(DIGITAL_INPUTS, digitalInputs, va_digital_inputs_t);
			syslog(LOG_NOTICE,"%s: Digital Inputs Configuration sent to Recording Agent.", __FUNCTION__);
		}
		else
		{
			syslog(LOG_NOTICE,"%s: No Digital Inputs Configured.", __FUNCTION__);
			return (EXIT_SUCCESS);
		}
	}
	else
	{
		syslog(LOG_CRIT,"%s: Invalid configuration. Msg not sent.", __func__);
	    return (EXIT_FAILURE);
	}
    return (EXIT_SUCCESS);
}//configuration_send_digital_inputs

#ifdef QNX_BUILD
void updateConfiguration(sLDARSConfiguration *configuration)
{
	return;
}
eConfigurationType getConfiguration(sLDARSConfiguration *config)
{
	eConfigurationType type = eConfigurationTypeV; 

#ifdef HARDCODED_CONFIGURATION
#if 0
    uint32_t       uint32_Variable = 0;
    uint32_t       loopIndex       = 0;
    uint32_t       connIndex       = 0;
    uint32_t       numIpsIndex     = 0;
    uint32_t       intfIndex       = 0;
    uint32_t       timeProtocol    = 0;

    eEthernetDuplexType  ethernetDuplex = eEthernetDuplexTypeAuto;
    eEthernetSpeedType   ethernetSpeed  = eEthernetSpeedTypeAuto;
    int                  result         = 0;

    uint8_t        numEthernetConnectors = 0;
    eDHCPType      dhcpType              = eDHCPTypeNone;
    uint8_t        numEthernetBridges    = 0;
    uint8_t        numConnections        = 0;
    uint8_t        numIps                = 0;
    uint8_t        numSerialConnectors   = 0;
    uint8_t        numClassCAddresses    = 0;
    uint8_t        numInterfaces         = 0;
    uint8_t        numPorts              = 0;

    char *         timeString;
    time_t         tempTime;
    uint8_t        temp_uint8_t;
    uint16_t       temp_uint16_t;
    uint32_t       temp_uint32_t;
    BOOL           temp_BOOL;

    sLDARSConfiguration                   segmentData;
    sLDARSConfiguration                  *config         = &segmentData;
    sUSBDownloadControlConfiguration     *usbDnldCtrlConfig      = NULL;
    sVVideoDownloadOverWebConfiguration  *videoDnldOverWebConfig = NULL;
	sSerialGPSConfiguration              *serialGPSConfig        = NULL;
#endif	

	// Configuration info.
	config->configuration_format_version = 3;
	strncpy(config->configuration_part_number, "37645P", MAX_CONFIG_PART_NUM_SIZE);
	strncpy(config->configuration_name, "3CAM_FG_LL", MAX_CONFIG_NAME_SIZE);
	strncpy(config->configuration_version, "01.00.00.00", MAX_CONFIG_VERSION_SIZE);

    // Ethernet Configuration

	// Number of ethernet connectors
	config->configuration.ethernetConfiguration.numEthernetConnectors = 7;
    
	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[0].name, "J3" , MAX_ETH_CONFIGURATION_NAME_STRING_SIZE);
    config->configuration.ethernetConfiguration.ethernetConnectors[0].dhcpType = eDHCPTypeServer;
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[0].ip, "10.10.9.39", MAX_IPv4_ADDRESS_STRING_SIZE);
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[0].netmask, "255.255.255.0", MAX_IPv4_ADDRESS_STRING_SIZE);
	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[0].defgateway, "10.10.9.254", MAX_IPv4_ADDRESS_STRING_SIZE);
	config->configuration.ethernetConfiguration.ethernetConnectors[0].powerOverEthernetEnabled = TRUE;	
	config->configuration.ethernetConfiguration.ethernetConnectors[0].ethernetDuplex = eEthernetDuplexTypeAuto;
	config->configuration.ethernetConfiguration.ethernetConnectors[0].ethernetSpeed = eEthernetSpeedTypeAuto;	
	config->configuration.ethernetConfiguration.ethernetConnectors[0].ethernetAutoMdiEnabled = TRUE;

	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[1].name, "J4" , MAX_ETH_CONFIGURATION_NAME_STRING_SIZE);
    config->configuration.ethernetConfiguration.ethernetConnectors[1].dhcpType = eDHCPTypeServer;
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[1].ip, "10.10.9.40", MAX_IPv4_ADDRESS_STRING_SIZE);
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[1].netmask, "255.255.255.0", MAX_IPv4_ADDRESS_STRING_SIZE);
	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[1].defgateway, "10.10.9.254", MAX_IPv4_ADDRESS_STRING_SIZE);
	config->configuration.ethernetConfiguration.ethernetConnectors[1].powerOverEthernetEnabled = TRUE;	
	config->configuration.ethernetConfiguration.ethernetConnectors[1].ethernetDuplex = eEthernetDuplexTypeAuto;
	config->configuration.ethernetConfiguration.ethernetConnectors[1].ethernetSpeed = eEthernetSpeedTypeAuto;	
	config->configuration.ethernetConfiguration.ethernetConnectors[1].ethernetAutoMdiEnabled = TRUE;

	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[2].name, "J5" , MAX_ETH_CONFIGURATION_NAME_STRING_SIZE);
    config->configuration.ethernetConfiguration.ethernetConnectors[2].dhcpType = eDHCPTypeNone;
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[2].ip, "10.10.9.43", MAX_IPv4_ADDRESS_STRING_SIZE);
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[2].netmask, "255.255.255.0", MAX_IPv4_ADDRESS_STRING_SIZE);
	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[2].defgateway, "10.10.9.254", MAX_IPv4_ADDRESS_STRING_SIZE);
	config->configuration.ethernetConfiguration.ethernetConnectors[2].powerOverEthernetEnabled = FALSE;	
	config->configuration.ethernetConfiguration.ethernetConnectors[2].ethernetDuplex = eEthernetDuplexTypeAuto;
	config->configuration.ethernetConfiguration.ethernetConnectors[2].ethernetSpeed = eEthernetSpeedTypeAuto;	
	config->configuration.ethernetConfiguration.ethernetConnectors[2].ethernetAutoMdiEnabled = TRUE;

	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[3].name, "J6" , MAX_ETH_CONFIGURATION_NAME_STRING_SIZE);
    config->configuration.ethernetConfiguration.ethernetConnectors[3].dhcpType = eDHCPTypeServer;
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[3].ip, "10.10.9.41", MAX_IPv4_ADDRESS_STRING_SIZE);
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[3].netmask, "255.255.255.0", MAX_IPv4_ADDRESS_STRING_SIZE);
	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[3].defgateway, "10.10.9.254", MAX_IPv4_ADDRESS_STRING_SIZE);
	config->configuration.ethernetConfiguration.ethernetConnectors[3].powerOverEthernetEnabled = TRUE;	
	config->configuration.ethernetConfiguration.ethernetConnectors[3].ethernetDuplex = eEthernetDuplexTypeAuto;
	config->configuration.ethernetConfiguration.ethernetConnectors[3].ethernetSpeed = eEthernetSpeedTypeAuto;	
	config->configuration.ethernetConfiguration.ethernetConnectors[3].ethernetAutoMdiEnabled = TRUE;

	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[4].name, "J7" , MAX_ETH_CONFIGURATION_NAME_STRING_SIZE);
    config->configuration.ethernetConfiguration.ethernetConnectors[4].dhcpType = eDHCPTypeServer;
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[4].ip, "10.10.9.42", MAX_IPv4_ADDRESS_STRING_SIZE);
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[4].netmask, "255.255.255.0", MAX_IPv4_ADDRESS_STRING_SIZE);
	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[4].defgateway, "10.10.9.254", MAX_IPv4_ADDRESS_STRING_SIZE);
	config->configuration.ethernetConfiguration.ethernetConnectors[4].powerOverEthernetEnabled = TRUE;	
	config->configuration.ethernetConfiguration.ethernetConnectors[4].ethernetDuplex = eEthernetDuplexTypeAuto;
	config->configuration.ethernetConfiguration.ethernetConnectors[4].ethernetSpeed = eEthernetSpeedTypeAuto;	
	config->configuration.ethernetConfiguration.ethernetConnectors[4].ethernetAutoMdiEnabled = TRUE;

	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[5].name, "J8" , MAX_ETH_CONFIGURATION_NAME_STRING_SIZE);
    config->configuration.ethernetConfiguration.ethernetConnectors[5].dhcpType = eDHCPTypeNone;
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[5].ip, "10.255.255.43", MAX_IPv4_ADDRESS_STRING_SIZE);
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[5].netmask, "255.255.255.0", MAX_IPv4_ADDRESS_STRING_SIZE);
	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[5].defgateway, "10.255.255.254", MAX_IPv4_ADDRESS_STRING_SIZE);
	config->configuration.ethernetConfiguration.ethernetConnectors[5].powerOverEthernetEnabled = FALSE;	
	config->configuration.ethernetConfiguration.ethernetConnectors[5].ethernetDuplex = eEthernetDuplexTypeAuto;
	config->configuration.ethernetConfiguration.ethernetConnectors[5].ethernetSpeed = eEthernetSpeedTypeAuto;	
	config->configuration.ethernetConfiguration.ethernetConnectors[5].ethernetAutoMdiEnabled = TRUE;

	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[5].name, "J9" , MAX_ETH_CONFIGURATION_NAME_STRING_SIZE);
    config->configuration.ethernetConfiguration.ethernetConnectors[5].dhcpType = eDHCPTypeNone;
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[5].ip, "10.10.9.44", MAX_IPv4_ADDRESS_STRING_SIZE);
    strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[5].netmask, "255.255.255.0", MAX_IPv4_ADDRESS_STRING_SIZE);
	strncpy(config->configuration.ethernetConfiguration.ethernetConnectors[5].defgateway, "10.10.9.254", MAX_IPv4_ADDRESS_STRING_SIZE);
	config->configuration.ethernetConfiguration.ethernetConnectors[5].powerOverEthernetEnabled = FALSE;	
	config->configuration.ethernetConfiguration.ethernetConnectors[5].ethernetDuplex = eEthernetDuplexTypeAuto;
	config->configuration.ethernetConfiguration.ethernetConnectors[5].ethernetSpeed = eEthernetSpeedTypeAuto;	
	config->configuration.ethernetConfiguration.ethernetConnectors[5].ethernetAutoMdiEnabled = TRUE;

	// DHCP Server Configuration
    config->configuration.ethernetConfiguration.DHCPServerConfiguration.enabled = TRUE;
    strncpy(config->configuration.ethernetConfiguration.DHCPServerConfiguration.poolMin, "10.10.9.100", MAX_IPv4_ADDRESS_STRING_SIZE);
	strncpy(config->configuration.ethernetConfiguration.DHCPServerConfiguration.poolMax, "10.10.9.200", MAX_IPv4_ADDRESS_STRING_SIZE);
                
	// Ethernet Bridge Configurations
	// Number of Ethernet Bridges
	config->configuration.ethernetConfiguration.numEthernetBridges = 1;
	config->configuration.ethernetConfiguration.ethernetBridgeConfigurations[0].numConnections = 2;
	config->configuration.ethernetConfiguration.ethernetBridgeConfigurations[0].bridgeConnections[0].numIPs = 6;
    strncpy(config->configuration.ethernetConfiguration.ethernetBridgeConfigurations[0].bridgeConnections[0].connectionIP[0].ip, "10.10.9.39" ,MAX_IPv4_ADDRESS_STRING_SIZE);
    strncpy(config->configuration.ethernetConfiguration.ethernetBridgeConfigurations[0].bridgeConnections[0].connectionIP[1].ip, "10.10.9.40" ,MAX_IPv4_ADDRESS_STRING_SIZE);
    strncpy(config->configuration.ethernetConfiguration.ethernetBridgeConfigurations[0].bridgeConnections[0].connectionIP[2].ip, "10.10.9.41" ,MAX_IPv4_ADDRESS_STRING_SIZE);	
    strncpy(config->configuration.ethernetConfiguration.ethernetBridgeConfigurations[0].bridgeConnections[0].connectionIP[3].ip, "10.10.9.42" ,MAX_IPv4_ADDRESS_STRING_SIZE);	
    strncpy(config->configuration.ethernetConfiguration.ethernetBridgeConfigurations[0].bridgeConnections[0].connectionIP[4].ip, "10.10.9.43" ,MAX_IPv4_ADDRESS_STRING_SIZE);	
    strncpy(config->configuration.ethernetConfiguration.ethernetBridgeConfigurations[0].bridgeConnections[0].connectionIP[5].ip, "10.10.9.44" ,MAX_IPv4_ADDRESS_STRING_SIZE);	
	
	config->configuration.ethernetConfiguration.ethernetBridgeConfigurations[0].bridgeConnections[1].numIPs = 1;
    strncpy(config->configuration.ethernetConfiguration.ethernetBridgeConfigurations[0].bridgeConnections[1].connectionIP[0].ip, "10.255.255.43" ,MAX_IPv4_ADDRESS_STRING_SIZE);		

    // Serial Configuration
    // Number of Serial Ports
	config->configuration.serialConfiguration.numSerialConnectors = 3;
    strncpy(config->configuration.serialConfiguration.serialConnectors[0].name, "RS232 #1", MAX_SERIAL_NAME_STRING_SIZE);
    config->configuration.serialConfiguration.serialConnectors[0].enabled = TRUE;
    strncpy(config->configuration.serialConfiguration.serialConnectors[1].name, "RS232 #2", MAX_SERIAL_NAME_STRING_SIZE);
    config->configuration.serialConfiguration.serialConnectors[1].enabled = FALSE;
    strncpy(config->configuration.serialConfiguration.serialConnectors[2].name, "Synchronous RS422", MAX_SERIAL_NAME_STRING_SIZE);
    config->configuration.serialConfiguration.serialConnectors[2].enabled = FALSE;

    //Class C Configuration
    config->configuration.classCConfiguration.numClassCAddresses = 1;
    strncpy(config->configuration.classCConfiguration.classCAddresses[0].ip, "239.255.0.128", MAX_IPv4_ADDRESS_STRING_SIZE);

    //Application Interface Configuration
	config->configuration.appIntfConfiguration.numInterfacesIETMS = 0;
	config->configuration.appIntfConfiguration.numInterfacesIITCS = 0;
	config->configuration.appIntfConfiguration.numInterfacesGenericClassD = 0;
	config->configuration.appIntfConfiguration.numInterfacesClassCLIG = 0;
	config->configuration.appIntfConfiguration.numInterfacesClassDLIG = 0;
	config->configuration.appIntfConfiguration.numInterfacesClassC = 0;

    // Serial Protocol
	config->configuration.appIntfConfiguration.serialProtocol = eSerialProtocolNone;

    // LSI Interface
	config->configuration.appIntfConfiguration.numInterfacesLSI = 0;

    // ACP Interface
	config->configuration.appIntfConfiguration.numInterfacesACP = 0;

    // Legacy Link Interface
    config->configuration.appIntfConfiguration.numInterfacesLegacyLink = 1;
	strncpy(config->configuration.appIntfConfiguration.interfaceConfigurationLegacyLink[0].applicationInfo.name, "Legacy Link", MAX_INTERFACE_NAME_SIZE);
	config->configuration.appIntfConfiguration.interfaceConfigurationLegacyLink[0].applicationInfo.controlLED = TRUE;
	config->configuration.appIntfConfiguration.interfaceConfigurationLegacyLink[0].applicationInfo.storageLocation = eStorageLocationFRA;
    strncpy(config->configuration.appIntfConfiguration.interfaceConfigurationLegacyLink[0].serialPort.name, "RS232 #1", MAX_SERIAL_NAME_STRING_SIZE);
	config->configuration.appIntfConfiguration.interfaceConfigurationLegacyLink[0].serialPort.enabled = TRUE;

    // LCCM Interface
	config->configuration.appIntfConfiguration.numInterfacesLCCM = 0;
    // Energy Management Interface
	config->configuration.appIntfConfiguration.numInterfacesEnergyManagement = 0;

    // TCP ECP Application Interface
	config->configuration.appIntfConfiguration.numInterfacesTCPECP = 0;
    // Class C ECP Application Interface
	config->configuration.appIntfConfiguration.numInterfacesClassCECP = 0;
            
	//Time Source Configuration
	config->configuration.timeSourceConfiguration.numSources = 1;
	config->configuration.timeSourceConfiguration.timeSources[0].protocol = eTimeProtocolGPRMC;
	config->configuration.timeSourceConfiguration.timeSources[0].priority = 0;	

    //Camera Configuration
	config->configuration.numCameraConfigurations = 3;
    
	config->configuration.cameraConfiguration[0].id = 1;
	strncpy(config->configuration.cameraConfiguration[0].name,"FORWARD",MAX_CAMERA_NAME_SIZE);
    // Camera settings during configuration.
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.resolutionDuringConfig = eVResolutionDuringConfig1080;
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.compressionDuringConfig = eVCompressionDuringConfig30;
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.frameRateDuringConfig = eVFrameRateDuringConfig30;                      
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.audioEnabledDuringConfig = TRUE;                                                
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.essential = TRUE;
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.accessoryPower = FALSE;                                                                                                
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.camera_max_recording_hours = 0; 
    // Camera Settings during event
    config->configuration.cameraConfiguration[0].settingsDuringEvent.cameraControlByEvent = FALSE;
	config->configuration.cameraConfiguration[0].settingsDuringEvent.numEventsForCameraControl = 0;

	config->configuration.cameraConfiguration[0].id = 2;
	strncpy(config->configuration.cameraConfiguration[0].name,"INWARD",MAX_CAMERA_NAME_SIZE);
    // Camera settings during configuration.
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.resolutionDuringConfig = eVResolutionDuringConfig720;
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.compressionDuringConfig = eVCompressionDuringConfig30;
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.frameRateDuringConfig = eVFrameRateDuringConfig18;                      
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.audioEnabledDuringConfig = TRUE;                                                
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.essential = TRUE;
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.accessoryPower = FALSE;                                                                                                
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.camera_max_recording_hours = 0; 
    // Camera Settings during event
    config->configuration.cameraConfiguration[0].settingsDuringEvent.cameraControlByEvent = FALSE;
	config->configuration.cameraConfiguration[0].settingsDuringEvent.numEventsForCameraControl = 0;

	config->configuration.cameraConfiguration[0].id = 3;
	strncpy(config->configuration.cameraConfiguration[0].name,"CONTROLS",MAX_CAMERA_NAME_SIZE);
    // Camera settings during configuration.
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.resolutionDuringConfig = eVResolutionDuringConfig720;
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.compressionDuringConfig = eVCompressionDuringConfig30;
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.frameRateDuringConfig = eVFrameRateDuringConfig10;                      
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.audioEnabledDuringConfig = TRUE;                                                
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.essential = TRUE;
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.accessoryPower = FALSE;                                                                                                
	config->configuration.cameraConfiguration[0].settingsDuringConfiguration.camera_max_recording_hours = 0; 
    // Camera Settings during event
    config->configuration.cameraConfiguration[0].settingsDuringEvent.cameraControlByEvent = FALSE;
	config->configuration.cameraConfiguration[0].settingsDuringEvent.numEventsForCameraControl = 0;

    //Digital Input Configuration
    config->configuration.numDigitalInputConfigurations = 3;
    strncpy(config->configuration.digitalInputConfiguration[0].name,"DIG1", MAX_DIGITAL_INPUT_NAME_SIZE);
    config->configuration.digitalInputConfiguration[0].number = 1;
    config->configuration.digitalInputConfiguration[0].recording = FALSE;
    strncpy(config->configuration.digitalInputConfiguration[1].name,"DIG2", MAX_DIGITAL_INPUT_NAME_SIZE);
    config->configuration.digitalInputConfiguration[1].number = 1;
    config->configuration.digitalInputConfiguration[1].recording = FALSE;
    strncpy(config->configuration.digitalInputConfiguration[2].name,"DIG3", MAX_DIGITAL_INPUT_NAME_SIZE);
    config->configuration.digitalInputConfiguration[2].number = 1;
    config->configuration.digitalInputConfiguration[2].recording = FALSE;

	//USB Download Control Configuration
	config->configuration.numUsbDownloadConfiguration = 0;

	// Serial GPS Configuration
	config->configuration.numSerialGPSConfiguration = 0;

				
    // Show Video Download over web configuration
    config->configuration.videoDownloadOverWebConfiguration.enabled = FALSE;
    config->configuration.videoDownloadOverWebConfiguration.duration = 	eVVideoWebDownloadDuration2Minutes;
#endif

	return (type);
}
#endif // #ifdef QNX_BUILD
