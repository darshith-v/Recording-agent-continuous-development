#ifndef _VRADUMP_H_
#define _VRADUMP_H_

#include <stdint.h>
#include <stddef.h>

#include "config.h"

#include "metadata_def.h"
#include "metadata_util.h"
#include "coc.h"

/* flags to indicate which stream is available inside a chunk header */
#define VIDEO_STREAM_AVAILABLE	0x01
#define AUDIO_STREAM_AVAILABLE	0x02

/* flags to check which stream is available */
#define IS_VIDEO_STREAM_PRESENT	0x01
#define IS_AUDIO_STREAM_PRESENT	0x02


/*
 * Raw packet type enumeration.
 * Used to supply the type(origin) of a raw packet
 * when conveying receive buffers from Source Thread to Sink Thread
 */
typedef enum
{
	RAWPACKET_VRTP = 0,		/* Video data stream - RTP */
	RAWPACKET_VRTCP = 1, 	/* Video control stream - RTCP */
	RAWPACKET_ARTP = 2,		/* Audio data stream - RTP */
	RAWPACKET_ARTCP = 3 	/* Audio control stream - RTCP */
} raw_packet_type_t;

typedef struct
{
	int 					sockfd;
	int 					non_blocking;
	raw_packet_type_t 		packet_type;
	char 					pkt_src_name[16];
	volatile unsigned 		*pcnt;
	volatile unsigned 		*pdrp;
} packet_source_t;

typedef struct {
	struct timeval32 {
		uint32_t tv_sec; /* start of recording (GMT) (seconds) */
		uint32_t tv_usec; /* start of recording (GMT) (microseconds)*/
	} start;
	uint32_t source; /* network source (multicast address) */
	uint16_t port; /* UDP port */
	uint8_t streams; /* streams */
	uint8_t padding; /* padding */
} vradump_chunk_hdr_t;

typedef struct {
	uint16_t length; /* length of packet, including this header (may be smaller than plen if not whole packet recorded) */
	uint16_t plen; /* actual header+payload length for RTP, 0 for RTCP */
	uint32_t offset; /* milliseconds since the start of recording */
} vradump_packet_t;

typedef union {
	struct {
		vradump_packet_t hdr;
		char data[BUFPOOL_BUF_SIZE - sizeof(vradump_packet_t)];
	} p;
	char byte[BUFPOOL_BUF_SIZE];
} vradump_buffer_t;

/*
 * vradump V2 file prefix:
 * 2048 bytes in size:
 *    first 1024 bytes:
 *        - 32-byte she-bang string
 *        - parameters
 *        - compressed SDP data
 *    next 1024 bytesL
 *        - I-frame index size (number of records)
 *        - I-frame index
 * ==========
 * Note that vradump chunk header structure goes here (on 2048-byte boundary)
 *
 */

/* keyframe index record */
typedef struct {
	unsigned int timestamp ;		/* epoch time, GMT */
	unsigned int byteoffset ;		/* offset from the beginning of the file */
} keyframe_idx_rcd_t ;

/* First 1024 bytes of 2048-byte prefix */
typedef struct subpfx_1 {
	char shebang[32] ;				/* a null-terminated she-bang string, for example: "#!vraplay2.0 10.10.9.102/554"   */
	unsigned int ext_status ;		/* bitmask providing additional details about this chunk */
	//
	unsigned short chunk_fr ;		/* video frame rate in frames per second */
	unsigned short chunk_hres ;		/* frame width in pixels used in this chunk */
	unsigned short chunk_vres ;		/* frame height in pixels used in this chunk */
	unsigned short spare ;			/* padding, for alighnment purposes only */

									/* information known when recordin to this file has started */
	meta_gps_memory_t 		current_gps; 	/* current(last known) GPS location */
	meta_time_memory_t 	    current_tm;		/* current(last known) time information */
	meta_event_memory_t 	current_evt;    /* current(last known) list of video events */
	meta_loco_id_t          current_locoId;	/* current (last known) loco ID */
 
	unsigned char payload_hash[MAX_KEY_LENGTH]; 			/* Assuming SHA-1  digest (160 bits – 20 bytes). 
				  		Assigning 256 bytes to align on a 8-byte boundary and future expansion */

	int sdpdata_size ;				/* size of the following compressed SDP data string */
} subpfx_1_t ;

typedef struct subpfx_2 {
	int max_records ;				/* index size, i.e. max number of index records allowed */
	int num_records ;				/* current number of keyframe idx records */
} subpfx_2_t ;

// Since the hash (256 bytes) has been added to the prefix the size of the header is also increased from 2048 bytes to 
// 2048 + 256 bytes. Hence each prefix size is increased by 128 bytes, i.e. from 1024 to 1024 + 128 = 1152 bytes
typedef union
{
	struct {
		/* First 1152 bytes of 2304-byte prefix */
		subpfx_1_t subpfx_1;
		unsigned char sdp_data[1152 - sizeof(struct subpfx_1)] ;		/* compressed SDP file contents */


		/* last 1024 bytes of 2048-byte prefix */
		subpfx_2_t subpfx_2;
		keyframe_idx_rcd_t keyframe_index[(1152 - sizeof(struct subpfx_2)) / sizeof(keyframe_idx_rcd_t)] ;
	} ;
	char bytes[2048 + MAX_KEY_LENGTH] ;
} vradump_file_pfx_t ;


extern void vradump_chunk_header(FILE *out, char *phost, int port, struct timeval *start, char *psdp, int sdp_size,  int actual_fps, int actual_hres, int actual_vres) ;
extern void *vradump_sink_thread(void *arg) ;
extern void *vradump_capture_thread(void *arg) ;
extern void vradump_save_keyframe_data(FILE *fp) ;
extern void vradump_reset_index_records(void) ;
extern void vradump_update_stream_availablity(FILE *fp, unsigned int stream_availablity) ;
extern void update_HMAC_SHA1_video(FILE *fp);
#ifdef DEBUG
void display_HMAC_SHA1_video(const char *filename);
#endif

#endif	// #ifndef _VRADUMP_H_
