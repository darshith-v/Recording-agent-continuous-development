#ifndef _METAINTF_H
#define _METAINTF_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "metadata_def.h"


extern int set_gps(int32_t latitude, int32_t longitude, int32_t speed);

extern int set_time_info(uint32_t system_time, int64_t time_offset, char * time_source);

extern int set_video_event(const char* digital_name, uint8_t digital_value);

extern int set_digital_input_config(const char* digital_name, int digital_number, bool digital_recording);

extern int get_digital_input_name(int digital_number, char* digital_name);

extern void init_metadata_recording_queue();
extern void deinit_metadata_recording_queue();
extern int get_metadata_descriptor(metadata_desc_t* pdesc); 

extern int set_locomotive_id(char * loco_id);

#endif