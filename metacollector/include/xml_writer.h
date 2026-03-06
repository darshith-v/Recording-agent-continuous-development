#ifndef _XML_WRITER_H
#define _XML_WRITER_H

#include "metadata_def.h"
#include "metareader.h"

extern int init_xml_document_declaration(FILE *file);

extern int write_gps_list(FILE *file, gps_record_t *p_gps, int count);
extern int write_event_list(FILE *file, video_event_record_t *p_event, int count);
extern int write_timeoffset_list(FILE *file, timeoffset_record_t *p_timeoffset, int count);
extern int write_corrupt_segment(FILE *file,unsigned long epochseconds,int duration_seconds);

extern void write_list_start(FILE *file, eMetaDataType type);
extern void write_list_close(FILE *file, eMetaDataType type);

#endif