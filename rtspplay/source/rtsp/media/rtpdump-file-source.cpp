

#include "rtpdump-file-source.h"
#include "cstringext.h"
#include "base64.h"
#include "rtp-profile.h"
#include "rtp-payload.h"
#include <assert.h>

enum 
{
    e_init = -1,
    e_seek = 0,
    e_play = 1
    
};


RTPdumpFileSource::RTPdumpFileSource(const std::string dir, const std::string file_type)
:m_reader(dir,file_type)
{

    m_dir = dir;
    m_type = file_type;
    m_time = (struct timeval){0};
    m_end = UINT32_MAX;
    m_first = -1;
    r = NULL;
    a_r = NULL;
    v_r = NULL;
    m_verbose = 0;
    m_begin = 0;
    m_wclockOffset[0] = 0;
    m_wclockOffset[1] = 0;
    m_wclockRef[0] = 0;
    m_wclockRef[1] = 0;
    m_seqOffset[0] = 0;
    m_seqOffset[1] = 0;
    m_seqRef[0] = 0;
    m_seqRef[1] = 0;
    m_speed = 1.0;
    memset(&buffer,0,sizeof(buffer));
    //pre-fetch first packet
    play_handler(e_init);    
}

RTPdumpFileSource::~RTPdumpFileSource()
{
    
}




int RTPdumpFileSource::SetTransport(const char* track, std::shared_ptr<IRTPTransport> transport)
{
    char * t = strstr((char*)track,"track0");
    if (t != NULL)
    {
        m_video_transport = transport;
    }
    else
        m_audio_transport = transport;
    return 0;
}

int RTPdumpFileSource::Play()
{
    struct timeval now;
    gettimeofday(&now, 0);
    
    //if (m_verbose)
        //printf( "TIMER: %ld.%.6ld NOW: %ld.%.6ld\n", m_time.tv_sec, m_time.tv_usec, now.tv_sec, now.tv_usec);
    /*
    condition  m_time.tv_sec - now.tv_sec > 1
    is workaround for vivotek 360 camera. it is observed that time offset in rtp packets
    are inconsistent. Because we are play the chunk in wallclock mood. if offset gap between two
    packets are greater than 1 sec, we will play the packet instead of waiting.
    */
    if (timercmp(&m_time,&now,<) || m_time.tv_sec - now.tv_sec > 1)
    {
        if (0 == play_handler(e_play))
        {
            fprintf(stderr, "m_pos %ld End of video Chunks\n",m_pos);
            return 1;
        }
    }

    m_status = 1;
    return 0;
}



int RTPdumpFileSource::Pause()
{
    m_status = 2;
    return 0;
}

int RTPdumpFileSource::Seek(int64_t pos)
{
    if (pos == 0) return 0;
#ifdef __DEBUG
    fprintf(stderr, "%s: pos %ld m_pos %ld %d\n",__func__,pos,m_pos, pos>m_pos?1:0 );
#endif
    int64_t new_pos;

    if (pos > m_pos)
    {
        new_pos = m_reader.setSeekPosition(pos);
        if (-1 == new_pos)
            return EXIT_FAILURE;
        else if (new_pos != 0)
            m_pos = new_pos;
        /*keep reading until condition meet*/
        while (pos > m_pos)    
            play_handler(e_seek);
    }
    else if (pos < m_pos)
    {
        
        m_pos = 0;
        m_rtp[0].ts = 0;
        m_rtp[1].ts = 0;

        m_rtp[0].seq = 0;
        m_rtp[1].seq = 0;

        

        m_wclockRef[0] = 0;
        m_wclockRef[1] = 0;
        

        memset(&buffer,0,sizeof(buffer));
        m_time = (struct timeval){0};
        m_end = UINT32_MAX;
        m_first = -1;
        r = NULL;
        m_reader.resetReader();

        //pre-fetch first packet
        play_handler(e_init);
        Seek(pos); 
        return 0;
    }

    return 0;
}

int RTPdumpFileSource::SetSpeed(double speed)
{
    m_speed = speed;
#ifdef __DEBUG
    fprintf(stderr, "%s: Speed %fx\n",__func__,speed );
#endif
    return 0;
}

int RTPdumpFileSource::GetDuration(int64_t& duration) const
{
    return m_reader.GetDuration(duration);
}

int RTPdumpFileSource::GetSDPMedia(std::string& sdp) const
{
    sdp = m_reader.GetSDP();
    return 0;
}

int RTPdumpFileSource::GetRTPInfo(const char* uri, char *rtpinfo, size_t bytes) const
{
    int n =0;
    // RTP-Info: url=rtsp://foo.com/bar.avi/streamid=0;seq=45102,
    //           url=rtsp://foo.com/bar.avi/streamid=1;seq=30211
    if (v_r != NULL)
    {

        n += snprintf(rtpinfo + n, bytes - n, "url=%s/track0;seq=%hu;rtptime=%u", uri, ntohs(m_rtp[0].seq), ntohl(m_rtp[0].ts));
    }
    else
        return -1;
    if (m_reader.AudioPresent && a_r != NULL)
    {
        rtpinfo[n++] = ',';
        n += snprintf(rtpinfo + n, bytes - n, "url=%s/track1;seq=%hu;rtptime=%u", uri, ntohs(m_rtp[1].seq), ntohl(m_rtp[1].ts));  
    }

#ifdef __DEBUG
        fprintf(stderr, "RTP_INFO: %s\n",rtpinfo );
#endif
    return 0;
}



/*
* Transmit RTP/RTCP packet on output socket and mark as read.
*/
void RTPdumpFileSource::play_transmit(int b, int track)
{
   if (track > 1)
        return;
   /*rshafiq:04/29/2020: we are skipping RTCP packet from VRADUMP. it is from camera. 
   When we are playing it back it can confuse the RTSP-Client. it is better to skip 
   these packets for now.*/
   if (b >= 0 && buffer.p.hdr.length && buffer.p.hdr.plen)
    {
        if (track == 0) // 0-video 
        {
            m_video_transport->Send(buffer.p.hdr.plen == 0 ? true : false,
                (void*)buffer.p.data, buffer.p.hdr.length);
        }
        else if (m_audio_transport && track == 1)// Audio
        {   
            m_audio_transport->Send(buffer.p.hdr.plen == 0 ? true : false,
                (void*)buffer.p.data, buffer.p.hdr.length);
        }
        
        //assert(r == (int)buffer.p.hdr.length);
        buffer.p.hdr.length = 0;
    }
    else if (buffer.p.hdr.plen == 0) //RTCP packet
        buffer.p.hdr.length = 0;

} /* play_transmit */




int RTPdumpFileSource::play_handler(int client)
{
    /* generation time of m_first played back p. */
    struct timeval now;           /* current time */
    struct timeval next;          /* next packet generation time */
    int b = (int)client;  /* buffer to be played now */
    int offset = 0;
    RD_packet_t *pph ;
    
    /* playback scheduled packet */
    if (b == e_play)
        play_transmit(b,m_track);

    /* If we are done, skip rest. */
    if (m_end == 0) return 0;

    /* Get next packet; try again if we haven't reached the m_begin time. */
    do {
        if (m_reader.readNextPacket(&buffer) == 0) return 0;
    } while (buffer.p.hdr.offset < m_begin );


    if (buffer.p.hdr.plen) 
            r = (rtp_hdr_t *)buffer.p.data;
    else if (b == e_init && r == NULL) //keep reading packets until find valid rtp-info.
            play_handler(e_init);

    pph = &(buffer.p.hdr) ;
    /*it is quick workaround to distinguish the audio RTP from video RTP*/
    if ((pph->offset) & 0x80000000) {
        pph->offset &= (~0x80000000) ; // reset the offset to original form
        m_track = 1; // audio track
        a_r = (rtp_hdr_t *)buffer.p.data; //it is audio buffer
        // update m_rtp[1] (audio) for later. will be send to client when needed
        
        if (m_wclockRef[1] == 0)
            m_wclockRef[1] = a_r->ts;
        

        if (m_wclockOffset[1]  == 0)
            m_wclockOffset[1]  = a_r->ts;
        else if (b == e_play)
        {
            uint32_t time_delta = ntohl(a_r->ts) - ntohl(m_wclockRef[1]);
            m_wclockOffset[1] += time_delta;
        }


        if (m_seqOffset[1] == 0)
            m_seqOffset[1] = a_r->seq;
        else if (b == e_play)
            m_seqOffset[1]+=1;
        
        
        m_wclockRef[1] = a_r->ts;

        m_rtp[1].ts = htonl((uint32_t)m_wclockOffset[1]);
        m_rtp[1].seq = htons(m_seqOffset[1]);
        a_r->ts = htonl((uint32_t)(m_wclockOffset[1]/(uint32_t)m_speed));
        a_r->seq = htons(m_seqOffset[1]);
    }
    else
    {
        // It is video RTP
        v_r = (rtp_hdr_t *)buffer.p.data;
        
        m_track = 0; //video track
        
        if (m_wclockRef[0] == 0)
            m_wclockRef[0] = v_r->ts;
        

        if (m_wclockOffset[0]  == 0)
            m_wclockOffset[0]  = v_r->ts;
        else if (b == e_play)
        {
            uint32_t time_delta = ntohl(v_r->ts) - ntohl(m_wclockRef[0]);
            m_wclockOffset[0] += time_delta;
        }


        if (m_seqOffset[0] == 0)
            m_seqOffset[0] = v_r->seq;
        else if (b == e_play)
            m_seqOffset[0] += 1;
        
        
        m_wclockRef[0] = v_r->ts;

        m_rtp[0].ts = htonl((uint32_t)m_wclockOffset[0]);
        m_rtp[0].seq = htons(m_seqOffset[0]);
        v_r->ts = htonl((uint32_t)(m_wclockOffset[0]/(uint32_t)m_speed));
        v_r->seq = htons(m_seqOffset[0]);
    }
    /*Keep reading file until find expected rtp packets
    VideoPresent and VideoPresent flags tell if to expect these packets from dump*/
    if (b == e_init && m_reader.VideoPresent && v_r == NULL) //keep reading packets until find Video rtp-info.
            play_handler(e_init);
    if (b == e_init && m_reader.AudioPresent && a_r == NULL) //keep reading packets until find Audio rtp-info.
            play_handler(e_init);

#ifdef __DEBUG
    if (m_verbose  && r != NULL)
    {
        gettimeofday(&now, 0);
        printf("! %1.3f %s(%3d;%3d) t=%6lu",
           (now.tv_sec + now.tv_usec/1e6), buffer.p.hdr.plen ? "RTP " : "RTCP",
           buffer.p.hdr.length, buffer.p.hdr.plen,
           (unsigned long)buffer.p.hdr.offset);
        printf(" track %d pt=%u ssrc=%8lx %cts=%9lu seq=%5u speed %f",m_track,
                (unsigned int)r->pt,
                (unsigned long)ntohl(r->ssrc), r->m ? '*' : ' ',
                (unsigned long)ntohl(r->ts), ntohs(r->seq),m_speed);
        printf(" b == %d\n",b);
    }
#endif

    /*
    * If new packet is after end of alloted time, don't insert into list
    * and set 'm_end' to zero to avoid reading any more packets from
    * file.
    */
    if (buffer.p.hdr.offset > m_end) {
        buffer.p.hdr.length = 0; /* erase again */
        m_end = 0;
        return 0;
    }
    /* Remember wall-clock and recording time of m_first valid packet. */
    if (m_first < 0) {
        gettimeofday(&now, 0);
        start = now;
        m_first = buffer.p.hdr.offset;
        m_offset = buffer.p.hdr.offset;
    }
    else if (b == e_seek)
    {
        gettimeofday(&now, 0);
        /*In seeking we will keep moving start and m_offset until meet the dest*/
        start = now;
        m_offset = buffer.p.hdr.offset;
    }

    /*match to fetch time of next rtp transmission*/
    offset = buffer.p.hdr.offset - m_offset;
    buffer.p.hdr.offset -= m_first;
    m_pos = buffer.p.hdr.offset;

    offset = offset / (int)m_speed ;
    /* RTCP or vat or playing back by wallclock: compute next playout time */
    next.tv_sec  = start.tv_sec  + offset/100;
    next.tv_usec = start.tv_usec + (offset%100) * 10000;

    if (next.tv_usec >= 1000000) {
        next.tv_usec -= 1000000;
        next.tv_sec  += 1;
    }
    m_time = next;
    return 1;
} /* play_handler */