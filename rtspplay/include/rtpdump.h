/*
 * (c) 1998-2018 by Columbia University; all rights reserved
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
* rtpdump file format
*
* The file starts with the tool to be used for playing this file,
* the multicast/unicast receive address and the port.
*
* #!rtpplay1.0 224.2.0.1/3456\n
*
* This is followed by one binary header (RD_hdr_t) and one RD_packet_t
* structure for each received packet.  All fields are in network byte
* order.  We don't need the source IP address since we can do mapping
* based on SSRC.  This saves (a little) space, avoids non-IPv4
* problems and privacy/security concerns. The header is followed by
* the RTP/RTCP header and (optionally) the actual payload.
*/

#ifndef _RTPDUMP_H_
#define _RTPDUMP_H_

#include <stdint.h>
#include <sys/sock.h>
#include "sysdep.h"

#define RTPFILE_VERSION "1.0"
#define VRAFILE_VERSION "2.0"

typedef struct {
  struct timeval32 {
      uint32_t tv_sec;    /* start of recording (GMT) (seconds) */
      uint32_t tv_usec;   /* start of recording (GMT) (microseconds)*/
  } start;
  uint32_t source;        /* network source (multicast address) */
  uint16_t port;          /* UDP port */
  uint16_t padding;       /* padding */
} RD_hdr_t;

typedef struct {
  uint16_t length;   /* length of packet, including this header (may
                        be smaller than plen if not whole packet recorded) */
  uint16_t plen;     /* actual header+payload length for RTP, 0 for RTCP */
  uint32_t offset;   /* milliseconds since the start of recording */
} RD_packet_t;

typedef union {
  struct {
    RD_packet_t hdr;
    char data[8000];
  } p;
  char byte[8192];
} RD_buffer_t;

/*
 * RTP data header
 */
typedef struct {
#if RTP_BIG_ENDIAN
    unsigned int version:2;   /* protocol version */
    unsigned int p:1;         /* padding flag */
    unsigned int x:1;         /* header extension flag */
    unsigned int cc:4;        /* CSRC count */
    unsigned int m:1;         /* marker bit */
    unsigned int pt:7;        /* payload type */
#else
    unsigned int cc:4;        /* CSRC count */
    unsigned int x:1;         /* header extension flag */
    unsigned int p:1;         /* padding flag */
    unsigned int version:2;   /* protocol version */
    unsigned int pt:7;        /* payload type */
    unsigned int m:1;         /* marker bit */
#endif
    unsigned int seq:16;      /* sequence number */
    uint32_t ts;               /* timestamp */
    uint32_t ssrc;             /* synchronization source */
    uint32_t csrc[1];          /* optional CSRC list */
} rtp_hdr_t;


#define MAX_FILE_COUNT   6000
#define MAX_FILE_NAME_SIZE 1024

#define BUFF_SIZE                           512
#define BUFF_SIZE_SMALL                     16
#define BUFF_SIZE_BIG                       4096
#define EPOCH_START_YEAR                    1900
#define EPOCH_START_MONTH                   1

typedef struct 
{
  char path[MAX_FILE_NAME_SIZE];
  int32_t duration;
  int64_t position;
}chunk_t;

typedef struct 
{
  chunk_t chunck_list[MAX_FILE_COUNT];
  uint16_t  total_chunk;
}video_chunk_t;

typedef struct  
{ 
   int sock[2];
   struct sockaddr_storage p_addr[2];
   int p_addrlen[2];
   int *play;
} rtpdump_thread_t;


#endif