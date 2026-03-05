#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_
#include "video_api.h"


#define UC_HAS_CAMERA_LIST 1
#define UC_HAS_TIME_INFO 2
#define UC_HAS_LOCOID 4

#define UC_MASK (UC_HAS_CAMERA_LIST | UC_HAS_TIME_INFO | UC_HAS_LOCOID)

/*
 * seconds to wait for UC to be sent from the Main APP
 */
#define UC_TIMEOUT	10

extern int uc_collect() ;
extern int uc_get_state() ;
extern int uc_is_ready() ;

char * uc_get_locoid() ;
camera_list_element_t * uc_get_camera_list() ;
time_info_t * uc_get_time_info() ;
ready_status_t * uc_get_ready_status();

#endif
