

#ifndef LVDAT_METADATA_H
#define LVDAT_METADATA_H
#ifndef VDOCK
#ifdef QNX_BUILD
#include <limits.h>
#else
#include <linux/limits.h>
#endif
#else
#include <limits.h>
#endif

#include "video_api.h"
#include <unistd.h> 


char metadata_start[] = {
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n \
<download>\n"};
char metadata_end[] = {"</download>\n"};

static const char *metadata_maindata = { \
"   <maindata>\n \
        <serial>%s</serial>\n \
        <locoid>%s</locoid>\n \
        <name>%s</name>\n \
        <model>%s</model>\n \
        <segment_count>%u</segment_count>\n \
        <starttime>%s</starttime>\n \
        <endtime>%s</endtime>\n \
        <starttimeoffset>%f</starttimeoffset>\n \
        <gpsformat>%s</gpsformat>\n \
        <mount_type>%u</mount_type>\n \
        <resolution>%s</resolution>\n \
        <compression>%s</compression>\n \
        <frame_rate>%s</frame_rate>\n \
        <signature>%s</signature>\n \
    </maindata>\n "};

char metadata_segments_start[] = { \
"   <segments>\n"};
char metadata_segments_end[] = {"   </segments>\n"};


char metadata_video_segment_start[] = { \
"        <segment>\n \
            <chunks>\n" };

char metadata_video_chunk[] = { \
"                 <chunk>\n \
                    <name>%s</name>\n \
                    <signature>%s</signature>\n \
                 </chunk>\n"};

char metadata_video_segment[] = { \
"             </chunks>\n \
            <start_time>%s</start_time>\n \
            <end_time>%s</end_time>\n \
        </segment>\n"};

char metadata_segment[] = { \
"       <segment>\n \
            <filename>%s</filename>\n \
            <start_time>%s</start_time>\n \
            <end_time>%s</end_time>\n \
            <signature>%s</signature>\n \
        </segment>\n"};
char metadata_corrupt_segments_start[] = { \
"   <corrupt_segments>\n"};
char metadata_corrupt_video_start[] = { \
"   <corrupt_video_segments>\n"};
char metadata_corrupt_time_range[] = { \
"       <time_range>\n \
            <start_time>%s</start_time>\n \
            <end_time>%s</end_time>\n \
        </time_range>\n "};
char metadata_corrupt_video_end[] = { \
"   </corrupt_video_segments>\n"};
char metadata_corrupt_meta_start[] = { \
"   <corrupt_meta_segments>\n"};
char metadata_corrupt_meta_end[] = { \
"   </corrupt_meta_segments>\n"};
char metadata_corrupt_segments_end[] = { \
"   </corrupt_segments>\n"};

char metadata_gps[] = { \
"   <gpsdata></gpsdata>\n"};
char metadata_video_event[] = { \
"   <video_events></video_events>\n"};

char metadata_time[] = { \
"   <time_change_list></time_change_list>\n"};

/* maindata definition */
typedef struct {
    double starttimeoffset;
    char signature[64];
    char locoid[LOCO_ID_STORAGE_SIZE];
    char model[64];
    char serial[32];
    char stattime[24];
    char endtime[24];
    char name[CAMERA_NAME_LENGTH+1];
    char  gpsformat[5];
    uint8_t mount_type;
    char resolution[16];
    char compression[16];
    char frame_rate[16];
    uint8_t  segment_count;
    
}maindata_t;

/* individual segment info */
typedef struct {
    char start_time[32];
    char end_time[32];
}segment_t;

/* individual segment info */
typedef struct {
    char name[256];
    char signature[256];
}lvdat_chunk_t;
#endif
