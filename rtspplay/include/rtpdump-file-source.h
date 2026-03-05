#pragma once //to fix recurrsive header issue, as flagged during SAST scan
#ifndef _RTPDUMP_file_source_h_
#define _RTPDUMP_file_source_h_

#include "rtpdump-file-source.h"
#include "rtpdump-file-reader.h"
#include "media-source.h"
#include "sys/process.h"
#include "time64.h"
#include <string>




class RTPdumpFileSource : public IMediaSource
{
public:
	RTPdumpFileSource(const std::string dir, const std::string file_type);
	virtual ~RTPdumpFileSource();

	int64_t getPosition(void) const {return m_pos;};

public:
	virtual int Play();
	virtual int Pause();
	virtual int Seek(int64_t pos);
	virtual int SetSpeed(double speed);
	virtual int GetDuration(int64_t& duration) const;
	virtual int GetSDPMedia(std::string& sdp) const;
	virtual int GetRTPInfo(const char* uri, char *rtpinfo, size_t bytes) const;
	virtual int SetTransport(const char* track, std::shared_ptr<IRTPTransport> transport);
private:
	std::shared_ptr<IRTPTransport> m_transport,m_audio_transport,m_video_transport;
	RTPdumpFileReader m_reader;
	struct timeval m_time;
	struct timeval start;

    int m_status;
    int m_track;
	int64_t m_pos;
	double m_speed;

	int m_verbose;        		/* be chatty about packets sent */
	uint32_t m_begin;      		/* time of first packet to send */
	uint32_t m_end; 			/* when to stop sending */
	int32_t m_first;         	/* time offset of first packet */
	uint32_t m_offset;
	uint32_t m_wclockOffset[2];
	uint32_t m_wclockRef[2];
	uint32_t m_seqOffset [2];
	uint32_t m_seqRef[2];
	RD_buffer_t buffer;
    rtp_hdr_t *r;
    rtp_hdr_t *a_r,*v_r;

    std::string m_dir;
    std::string m_type;

    struct rtpinfo_t
    {
    	uint32_t ts;
    	uint32_t seq;
    };
    rtpinfo_t m_rtp[2]; // 0-video, 1-audio

private:
//	int read_dir(video_chunk_t *data, const char *in_dir, const char *pat);
	int play_handler(int client);
	void play_transmit(int b, int track);
};

#endif /* !_RTPDUMP_file_source_h_ */
