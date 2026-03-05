
#include "cstringext.h"
#include "sys/sock.h"
#include "sys/system.h"
#include "sys/path.h"
#include "sys/sync.hpp"
#include "aio-worker.h"
#include "ctypedef.h"
#include "ntp-time.h"
#include "rtp-profile.h"
#include "rtsp-server.h"
#include "rtpdump-file-source.h"
#include "rtp-udp-transport.h"
#include "rtp-tcp-transport.h"
#include "rtsp-server-aio.h"
#include "uri-parse.h"
#include "urlcodec.h"
#include "path.h"
#include <map>
#include <memory>
#include "cpm/shared_ptr.h"


char root_directory[512] = {'\0'};

static const char* s_workdir = root_directory;
int return_code =0;
static ThreadLocker s_locker;



struct rtsp_media_t
{
	std::shared_ptr<IMediaSource> media;
	std::shared_ptr<IRTPTransport> transport;
	uint8_t channel; // rtp over rtsp interleaved channel
	int status; // setup-init, 1-play, 2-pause
	int64_t duration;
	std::string uri;
	rtsp_server_t* rtsp;
};


typedef std::map<std::string, rtsp_media_t> TSessions;
static TSessions s_sessions;

struct TFileDescription
{
	int64_t duration;
	std::string sdpmedia;
};
static std::map<std::string, TFileDescription> s_describes;

/* rtsp_extract_uuid
 * function to extract the UUID from a standard rtsp URI
 *
 * @param[in]   uri - URI to extract UUID from
 * @param[out]  return_string - \0 if URI string does not contain correct path, UUID otherwise
 */

std::string rtsp_extract_uuid(std::string uri)
{
    
    std::string temp_uri = uri;
    std::string return_string;
    std::string::size_type last_slash = -1, valid_string = -1;
    const std::string start_template = "rtsp://10.10.9.43:55400/playback/";

    if (temp_uri.find(start_template) == std::string::npos)
    {
        fprintf(stderr, "%s: Unable to find template in URI %s", __func__, uri.c_str());
        return_string = "\0";

    }
    else
    {
        return_string = temp_uri.substr(start_template.length()); // is now UUID/CAMNAME.vra
        last_slash = return_string.find("/");

        return_string = return_string.substr(0, last_slash);  // is now UUID
    }

    return return_string;
}


/* rtsp_delete_dir
 * function to delete a given path
 *
 * @param[in]   path - absolute path to delete
 * @param[out]  rc - return code of the rm system command
 */
int rtsp_delete_dir(std::string path)
{
    std::string command = "rm -rf ";
    int rc = -1;
    command += path;
    
    rc = system(command.c_str());

    return rc;
}

std::shared_ptr<RTPdumpFileSource> RTPdumpFileSource_static_cast(std::shared_ptr<IMediaSource> item)
{
     return std::static_pointer_cast<RTPdumpFileSource>(item);
}

static int rtsp_uri_parse(const char* uri, std::string& path)
{
	char path1[256];
	struct uri_t* r = uri_parse(uri, strlen(uri));
	if(!r)
		return -1;

	url_decode(r->path, strlen(r->path), path1, sizeof(path1));
	path = path1;
	uri_free(r);
	return 0;
}

static int rtsp_ondescribe(void* /*ptr*/, rtsp_server_t* rtsp, const char* uri)
{
	static const char* pattern_vod =
		"v=0\n"
		"o=- %llu %llu IN IP4 %s\n"
		"s=%s\n"
		"t=0 0\n"
		"a=range:npt=0-%.1f\n";
	
    std::string src_dir;
	std::map<std::string, TFileDescription>::const_iterator it;

	rtsp_uri_parse(uri, src_dir);
	if (strstartswith(src_dir.c_str(), "/playback/"))
	{
		char *t = NULL;
		src_dir = path::join(s_workdir, src_dir.c_str() + 10);
		t = strstr((char *)src_dir.c_str(), ".vra");
		*t = '\0';
		t = strrchr((char *)src_dir.c_str(), '/') + 1;
		*t = '\0';
	}
	else
	{
		assert(0);
		return -1;
	}
	TFileDescription describe;
	char buffer[1024] = { 0 };

	std::shared_ptr<IMediaSource> source;
	{
		source.reset(new RTPdumpFileSource(src_dir,"ra"));
		/*RS: Get the duration from the raw files*/
		source->GetDuration(describe.duration);
		

		int offset = snprintf(buffer, sizeof(buffer), pattern_vod, ntp64_now(), ntp64_now(), "0.0.0.0", uri, (describe.duration / 1000.0));
		assert(offset > 0 && offset + 1 < (int)sizeof(buffer));
	}

	source->GetSDPMedia(describe.sdpmedia);
    
	std::string sdp = buffer;
	sdp += describe.sdpmedia;
    //fprintf(stderr, "%s\n", sdp.c_str());
    return rtsp_server_reply_describe(rtsp, 200, sdp.c_str());

}

static int rtsp_onsetup(void* /*ptr*/, rtsp_server_t* rtsp, const char* uri, const char* session, const struct rtsp_header_transport_t transports[], size_t num)
{
	std::string src_dir = "";
	char rtsp_transport[128] = {0};
	char rtspsession[32] = {0};
	const struct rtsp_header_transport_t *transport = NULL;

	rtsp_uri_parse(uri, src_dir);
	if (strstartswith(src_dir.c_str(), "/playback/"))
	{
		char *t = NULL;
		src_dir = path::join(s_workdir, src_dir.c_str() + 10);
		t = strstr((char *)src_dir.c_str(), ".vra");
		*t = '\0';
		t = strrchr((char *)src_dir.c_str(), '/') + 1;
		*t = '\0';
		
	}
	else
	{
		// 459 Aggregate Operation Not Allowed
		return rtsp_server_reply_setup(rtsp, 459, NULL, NULL);
		
	}
/*
	if ('\\' == *filename.rbegin() || '/' == *filename.rbegin())
		filename.erase(filename.end() - 1);

	const char* basename = path_basename(filename.c_str());
	if (NULL == strchr(basename, '.')) // filter track1
		filename.erase(basename - filename.c_str() - 1, std::string::npos);
*/
	TSessions::iterator it;
	if(session)
	{
		AutoThreadLocker locker(s_locker);
		it = s_sessions.find(session);
		if(it == s_sessions.end())
		{
			// 454 Session Not Found
			return rtsp_server_reply_setup(rtsp, 454, NULL, NULL);
		}
		else
		{
			// don't support aggregate control
			if (0)
			{
				// 459 Aggregate Operation Not Allowed
				return rtsp_server_reply_setup(rtsp, 459, NULL, NULL);
			}
		}
	}
	else
	{
		rtsp_media_t item = {0};

		item.media.reset(new RTPdumpFileSource(src_dir,"ra"));

		
		snprintf(rtspsession, sizeof(rtspsession), "%p", item.media.get());
		AutoThreadLocker locker(s_locker);
		it = s_sessions.insert(std::make_pair(rtspsession, item)).first;
	}
	assert(NULL == transport);
	for(size_t i = 0; i < num && !transport; i++)
	{
		if(RTSP_TRANSPORT_RTP_UDP == transports[i].transport)
		{
			// RTP/AVP/UDP
			transport = &transports[i];
		}
		else if(RTSP_TRANSPORT_RTP_TCP == transports[i].transport)
		{
			// RTP/AVP/TCP
			// 10.12 Embedded (Interleaved) Binary Data (p40)
			transport = &transports[i];
		}
	}
	if(!transport)
	{
		// 461 Unsupported Transport
		return rtsp_server_reply_setup(rtsp, 461, NULL, NULL);
	}
	rtsp_media_t &item = it->second;
	item.media->GetDuration(item.duration);
	if (RTSP_TRANSPORT_RTP_TCP == transport->transport)
	{
		// 10.12 Embedded (Interleaved) Binary Data (p40)
		int interleaved[2];
		if (transport->interleaved1 == transport->interleaved2)
		{
			interleaved[0] = item.channel++;
			interleaved[1] = item.channel++;
		}
		else
		{
			interleaved[0] = transport->interleaved1;
			interleaved[1] = transport->interleaved2;
		}
		item.transport = std::make_shared<RTPTcpTransport>(rtsp, interleaved[0], interleaved[1]);
		item.media->SetTransport(path_basename(uri), item.transport);
		

		// RTP/AVP/TCP;interleaved=0-1
		snprintf(rtsp_transport, sizeof(rtsp_transport), "RTP/AVP/TCP;interleaved=%d-%d", interleaved[0], interleaved[1]);		
	}
	else if(transport->multicast)
	{
		// RFC 2326 1.6 Overall Operation p12
		// Multicast, client chooses address
		// Multicast, server chooses address
		// 461 Unsupported Transport
		return rtsp_server_reply_setup(rtsp, 461, NULL, NULL);
	}
	else
	{
		// unicast
		item.transport = std::make_shared<RTPUdpTransport>();

		assert(transport->rtp.u.client_port1 && transport->rtp.u.client_port2);
		unsigned short port[2] = { transport->rtp.u.client_port1, transport->rtp.u.client_port2 };
		const char *ip = transport->destination[0] ? transport->destination : rtsp_server_get_client(rtsp, NULL);
		if(0 != ((RTPUdpTransport*)item.transport.get())->Init(ip, port))
		{
			// log

			// 500 Internal Server Error
			return rtsp_server_reply_setup(rtsp, 500, NULL, NULL);
		}
		item.media->SetTransport(path_basename(uri), item.transport);

		// RTP/AVP;unicast;client_port=4588-4589;server_port=6256-6257;destination=xxxx
		snprintf(rtsp_transport, sizeof(rtsp_transport), 
			"RTP/AVP;unicast;client_port=%hu-%hu;server_port=%hu-%hu%s%s", 
			transport->rtp.u.client_port1, transport->rtp.u.client_port2,
			port[0], port[1],
			transport->destination[0] ? ";destination=" : "",
			transport->destination[0] ? transport->destination : "");

		//fprintf(stderr, "\n\n%s\n",rtsp_transport );
	}
    return rtsp_server_reply_setup(rtsp, 200, it->first.c_str(), rtsp_transport);
}

static int rtsp_onplay(void* /*ptr*/, rtsp_server_t* rtsp, const char* uri, const char* session, const int64_t *npt, const double *scale)
{
	std::shared_ptr<IMediaSource> source;
	std::string t_uri(uri);
	TSessions::iterator it;
	{
		AutoThreadLocker locker(s_locker);
		it = s_sessions.find(session ? session : "");
		if(it == s_sessions.end())
		{
			// 454 Session Not Found
			return rtsp_server_reply_play(rtsp, 454, NULL, NULL, NULL);
		}
		else
		{
			// uri with track
			if (0)
			{
				// 460 Only aggregate operation allowed
				return rtsp_server_reply_play(rtsp, 460, NULL, NULL, NULL);
			}
		}

		source = it->second.media;
		it->second.uri = t_uri;
		it->second.rtsp = rtsp;
	}
	if(npt && 0 != source->Seek(*npt))
	{
		// 457 Invalid Range
		//return rtsp_server_reply_play(rtsp, 457, NULL, NULL, NULL);
		npt = NULL;//source->getPosition();
	}
	if(scale && 0 != source->SetSpeed(*scale))
	{
		// set speed
		assert(*scale > 0);

		// 406 Not Acceptable
		return rtsp_server_reply_play(rtsp, 406, NULL, NULL, NULL);
	}
	// RFC 2326 12.33 RTP-Info (p55)
	// 1. Indicates the RTP timestamp corresponding to the time value in the Range response header.
	// 2. A mapping from RTP timestamps to NTP timestamps (wall clock) is available via RTCP.
	char rtpinfo[512] = { 0 };
	source->GetRTPInfo(uri, rtpinfo, sizeof(rtpinfo));
	it->second.status = 1;

	return rtsp_server_reply_play(rtsp, 200, npt, &(it->second.duration), rtpinfo);
}

static int rtsp_onpause(void* /*ptr*/, rtsp_server_t* rtsp, const char* /*uri*/, const char* session, const int64_t* npt)
{
	std::shared_ptr<IMediaSource> source;
	TSessions::iterator it;
	{
		AutoThreadLocker locker(s_locker);
		it = s_sessions.find(session ? session : "");
		if(it == s_sessions.end())
		{
			// 454 Session Not Found
			return rtsp_server_reply_pause(rtsp, 454);
		}
		else
		{
			// uri with track
			if (0)
			{
				// 460 Only aggregate operation allowed
				return rtsp_server_reply_pause(rtsp, 460);
			}
		}

		source = it->second.media;
		it->second.status = 2;
	}

	source->Pause();
	//fprintf(stderr, "%s: npt = %ld\n",__func__,npt );
	// 457 Invalid Range
    return rtsp_server_reply_pause(rtsp, 200);
}

static int rtsp_onteardown(void* /*ptr*/, rtsp_server_t* rtsp, const char* /*uri*/, const char* session)
{
	std::shared_ptr<IMediaSource> source;

    std::string uuid_string;
    std::string path_string(root_directory);        
	TSessions::iterator it;
	{
		AutoThreadLocker locker(s_locker);
		it = s_sessions.find(session ? session : "");
		if(it == s_sessions.end())
		{
			// 454 Session Not Found
			return rtsp_server_reply_teardown(rtsp, 454);
		}

        // grab session UUID before deleting session    
        uuid_string = rtsp_extract_uuid(it->second.uri);
        if (uuid_string != "\0")
        {
            path_string.append(uuid_string);
            if (rtsp_delete_dir(path_string) != 0)
            {
                fprintf(stderr, "Unable to delete UUID Folder %s", uuid_string.c_str());
            }
            else
            {
                fprintf(stderr, "Deleted UUID Folder %s", uuid_string.c_str());
            }
        }
        else
        {
            fprintf(stderr, "Unable to delete UUID Folder %s", uuid_string.c_str());
        }

		source = it->second.media;
		fprintf(stderr, "Session %s Erased!\n",it->first.c_str() );
		source = nullptr;
		s_sessions.erase(it);

       
	}
	rtsp_server_reply_teardown(rtsp, 200);
	return 0;
}

static int rtsp_onannounce(void* /*ptr*/, rtsp_server_t* rtsp, const char* uri, const char* sdp)
{
    return rtsp_server_reply_announce(rtsp, 200);
}

static int rtsp_onrecord(void* /*ptr*/, rtsp_server_t* rtsp, const char* uri, const char* session, const int64_t *npt, const double *scale)
{
    return rtsp_server_reply_record(rtsp, 200, NULL, NULL);
}

static int rtsp_onoptions(void* ptr, rtsp_server_t* rtsp, const char* uri)
{
	//const char* require = rtsp_server_get_header(rtsp, "Require");
	return rtsp_server_reply_options(rtsp, 200);
}

static int rtsp_ongetparameter(void* ptr, rtsp_server_t* rtsp, const char* uri, const char* session, const void* content, int bytes)
{
	//const char* ctype = rtsp_server_get_header(rtsp, "Content-Type");
	//const char* encoding = rtsp_server_get_header(rtsp, "Content-Encoding");
	//const char* language = rtsp_server_get_header(rtsp, "Content-Language");
	return rtsp_server_reply_get_parameter(rtsp, 200, NULL, 0);
}

static int rtsp_onsetparameter(void* ptr, rtsp_server_t* rtsp, const char* uri, const char* session, const void* content, int bytes)
{
	//const char* ctype = rtsp_server_get_header(rtsp, "Content-Type");
	//const char* encoding = rtsp_server_get_header(rtsp, "Content-Encoding");
	//const char* language = rtsp_server_get_header(rtsp, "Content-Language");
	return rtsp_server_reply_set_parameter(rtsp, 200);
}

static int rtsp_onclose(void* /*ptr2*/)
{
	// TODO: notify rtsp connection lost
	//       start a timer to check rtp/rtcp activity
	//       close rtsp media session on expired

	printf("rtsp close\n");
	return 0;
	
}

static void rtsp_onerror(void* /*param*/, rtsp_server_t* rtsp, int code)
{
	return_code = code;
	printf("rtsp_onerror code=%d, rtsp=%p\n", code, rtsp);

}

#define N_AIO_THREAD 4
extern "C" void ra_rtsp()
{
	aio_worker_init(N_AIO_THREAD);

	struct aio_rtsp_handler_t handler;
    std::string uuid_string;
    std::string base_string(root_directory);
    std::string path_string;
	memset(&handler, 0, sizeof(handler));
	handler.base.ondescribe = rtsp_ondescribe;
    handler.base.onsetup = rtsp_onsetup;
    handler.base.onplay = rtsp_onplay;
    handler.base.onpause = rtsp_onpause;
    handler.base.onteardown = rtsp_onteardown;
	handler.base.close = rtsp_onclose;
    handler.base.onannounce = rtsp_onannounce;
    handler.base.onrecord = rtsp_onrecord;
	handler.base.onoptions = rtsp_onoptions;
	handler.base.ongetparameter = rtsp_ongetparameter;
	handler.base.onsetparameter = rtsp_onsetparameter;
//	handler.base.send; // ignore
	handler.onerror = rtsp_onerror;
	void* tcp = rtsp_server_listen(NULL, 55400, &handler, NULL); assert(tcp);
	//void* udp = rtsp_transport_udp_create(NULL, 8558, &handler, NULL); assert(udp);

    while(1)
    {
		system_sleep(2);

		TSessions::iterator it;
		AutoThreadLocker locker(s_locker);
		for(it = s_sessions.begin(); it != s_sessions.end(); ++it)
		{
			rtsp_media_t &session = it->second;
			if(1 == session.status)
			{
				if (1 == session.media->Play())
				{
					
                    // grab session UUID before deleting session    
                    uuid_string = rtsp_extract_uuid(session.uri);
                    if (uuid_string != "\0")
                    {
                        path_string = base_string + uuid_string;        // updated to prevent double uuid string
                        if (rtsp_delete_dir(path_string) != 0)
                        {
                            fprintf(stderr, "Unable to delete UUID Folder %s", uuid_string.c_str());
                        }
                        else
                        {
                            fprintf(stderr, "Deleted UUID Folder %s", uuid_string.c_str());
                        }
                    }
                    else
                    {
                        fprintf(stderr, "Unable to delete UUID Folder %s", uuid_string.c_str());
                    }
                    // Video End removed the session
                    fprintf(stderr, "RTSP Server: Clearing Session %s\n", it->first.c_str());
					session.media = nullptr;
					s_sessions.erase(it);
				}
			}
		}
    }
    //! Code never reaches here
	aio_worker_clean(N_AIO_THREAD);
	rtsp_server_unlisten(tcp);
	//rtsp_transport_udp_destroy(udp);
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		//fprintf(stderr, "Default root directory used : /tmp/playback\n");
		strcpy(root_directory,"/tmp/playback/");
	}
	else
	{
		if (strlen(argv[1]) < sizeof(root_directory))
		{
			memcpy(root_directory, argv[1], strlen(argv[1]));
		}
		else
		{
			memcpy(root_directory, argv[1], sizeof(root_directory));
		}
	}

    // on new rtspplay process start, 
	ra_rtsp();
	return 0;
}