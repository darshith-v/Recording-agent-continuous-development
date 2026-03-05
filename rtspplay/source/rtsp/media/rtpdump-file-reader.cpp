
#include "rtpdump-file-reader.h"
#include <assert.h>
#include <string.h>
#include <algorithm>
#include <dirent.h>
#include <sstream>
#include <unistd.h>
#include <syslog.h>



extern "C" int unishox1_decompress(const char *in, int len, char *out, struct us_lnk_lst *prev_lines);

RTPdumpFileReader::RTPdumpFileReader(const std::string dir, const std::string file_type)
{

    m_activeFile = 0;
    m_duration = 0;
    m_index = 0;
    m_verbose = 0;
    m_capacity = 0;
    m_ptr = NULL;
    memset(&m_videoData, 0, sizeof(m_videoData));

    AudioPresent = 0;
    VideoPresent = 0;

    if (read_dir(&m_videoData,dir.c_str(),file_type.c_str())) {
        perror(optarg);
        return;
    }
    if ( m_videoData.total_chunk <= 0 )
    {
        fprintf(stderr,"Invalid file count\n");
        return;
    }
    sortChunks(&m_videoData);
#ifdef __DEBUG
    printf("Total chunk Found %d\n",m_videoData.total_chunk);
#endif    
    if (EXIT_FAILURE == openChunk())
    {
        fprintf(stderr, "Unable to read source directory\n");
        return;
    }

    struct sockaddr_in sin;
    struct timeval start;
    /* read header of input file */
    if (VRA_header(m_activeFile, &sin, &start, m_verbose) < 0) {
        fprintf(stderr, "Invalid header\n");
        return;
    }
}

int RTPdumpFileReader::resetReader(void)
{
    m_index = 0;
    if (EXIT_FAILURE == openChunk())
    {
        fprintf(stderr, "Unable to read source directory\n");
        return EXIT_FAILURE;
    }
    struct sockaddr_in sin = {0};
    struct timeval start;
    /* read header of input file */
    if (VRA_header(m_activeFile, &sin, &start, m_verbose) < 0) {
        fprintf(stderr, "Invalid header\n");
        return EXIT_FAILURE;
    }
    return (EXIT_SUCCESS);
}

int RTPdumpFileReader::setSeekPosition(int64_t position)
{
    if (m_index < m_videoData.total_chunk - 1)
    {
        if (position >= m_videoData.chunck_list[m_index].position && position <= m_videoData.chunck_list[m_index+1].position )
        {
            #ifdef __DEBUG
            fprintf(stderr, "%s: new position 0 \n",__func__);
            #endif
            return 0;
        }
    }
    for(int i= 0; i < m_videoData.total_chunk - 1; i++)
    {
        if (position >= m_videoData.chunck_list[i].position && position <= m_videoData.chunck_list[i+1].position)
        {
            m_index = i;
            if (EXIT_FAILURE == openChunk())
                return -1;
            struct sockaddr_in sin = {0};
            struct timeval start;
            /* read header of input file */
            if (VRA_header(m_activeFile, &sin, &start, m_verbose) < 0) {
                fprintf(stderr, "Invalid header\n");
                return - 1;
            }
            fprintf(stderr, "%s: new position %ld \n",__func__,m_videoData.chunck_list[i].position );
            return m_videoData.chunck_list[i].position;
        }
    }
    return -1;
}

RTPdumpFileReader::~RTPdumpFileReader()
{    
    fclose(m_activeFile);
}

int RTPdumpFileReader::read_dir(video_chunk_t *data, const char *in_dir, const char *pat)
{
    DIR* FD;
    struct dirent* in_file;
    uint16_t file_count = 0;
    char link[MAX_FILE_NAME_SIZE] = {0},*p_duration = NULL;

    /* Scanning the in directory */
    if (NULL == (FD = opendir (in_dir))) 
    {
        fprintf(stderr, "Error : Failed to open input directory - %s\n", strerror(errno));
        return 1;
    }
    while ((in_file = readdir(FD))) 
    {
        if (!strcmp (in_file->d_name, "."))
            continue;
        if (!strcmp (in_file->d_name, ".."))    
           continue;
#ifndef QNX_BUILD
        if (in_file->d_type != DT_LNK)
            continue;
#endif

        // It is link. lets find source of link!
        if ((strlen(in_dir) + strlen(in_file->d_name) + strlen("/")) > (MAX_FILE_NAME_SIZE - 1))
        {
            syslog(LOG_ERR, "%s: Not enough space to concatenate strings.\n", __func__);
            closedir(FD);
            return (EXIT_FAILURE);
        }
        memcpy(link, in_dir, strlen(in_dir));
        strcat(link, "/");
        strcat(link, in_file->d_name);

        char linkname[MAX_FILE_NAME_SIZE]={'\0'};
        ssize_t r = readlink(link, linkname, MAX_FILE_NAME_SIZE - 1);

        if (r < 0)
        { 
            perror("lstat");
            continue;
        }

        if(strstr(linkname,pat) == NULL)
           continue;

        
        if (m_verbose)
           printf("File[%d]: %s\n",file_count,linkname);
        strncpy(data->chunck_list[file_count].path,linkname,MAX_FILE_NAME_SIZE); 
        p_duration = strrchr(linkname,'/') + 1;
        p_duration = strchr(p_duration,'.') + 1;
        data->chunck_list[file_count++].duration = atol(p_duration)/1000;
        m_duration = m_duration + atoi(p_duration)/1000; 
    }
    if (file_count == 0)
    {
        closedir(FD);
        return (EXIT_FAILURE);
    }
    data->total_chunk = file_count;
    closedir(FD);

    return (EXIT_SUCCESS);
}

int RTPdumpFileReader::openChunk()
{
    if (m_index >= m_videoData.total_chunk) return EXIT_FAILURE;
    
    if(m_activeFile)
        fclose(m_activeFile);
#ifdef __DEBUG
    fprintf(stderr, "%s:%d: %s \n",__func__,__LINE__,m_videoData.chunck_list[m_index].path );
#endif
	if(!(m_activeFile = fopen(m_videoData.chunck_list[m_index].path, "rb")))
    {
        perror(m_videoData.chunck_list[m_index].path);
        return EXIT_FAILURE;
    }
    return (EXIT_SUCCESS);
}


int RTPdumpFileReader::readNextPacket(RD_buffer_t *b)
{
    int read_bytes = 0;
    while ((read_bytes = RD_read(m_activeFile, b)) == 0)
    {
        if (m_index++ <= m_videoData.total_chunk )
        {
           fclose(m_activeFile);
           m_activeFile = NULL; // to avoid double fclose() in othrer place
           if (EXIT_FAILURE == openChunk())
                return (EXIT_SUCCESS);

           struct sockaddr_in sin = {0};
           struct timeval start;
#ifdef __DEBUG
           printf("Playing video chunk [%d/%d]\n", m_index, m_videoData.total_chunk);
#endif
            /* Seek to the beginning of the file */
            fseek(m_activeFile, 0, SEEK_SET);

            if (VRA_header(m_activeFile, &sin, &start, 0) < 0) {
                fprintf(stderr, "Invalid header\n");
                continue;
            }
        }
        else break;
    }
    return read_bytes;
}



/*
* Read header. Return EXIT_FAILURE if not valid, 0 if ok.
*/
int RTPdumpFileReader::VRA_header(FILE *in, struct sockaddr_in *sin, struct timeval *start, int verbose)
{
    char line[80] = {0}, vramagic[80] = {0};
    vradump_chunk_hdr_t rdhdr;
    vradump_file_pfx_t file_pfx ;
    time_t tt;

    if (fgets(line, sizeof(line), in) == NULL) return EXIT_FAILURE;
    if (line[strlen(line)-1] == '\n') {
        line[strlen(line)-1] = ' ' ;
    }
    sprintf(vramagic, "#!vraplay%s ", VRAFILE_VERSION);
    if (strncmp(line, vramagic, strlen(vramagic)) != 0) return EXIT_FAILURE;

    char sdpstring[1500] = {0};
    rewind(in) ;
    
    if ((fread((char *)&file_pfx, sizeof(file_pfx), 1, in)) != 1) {
        perror("read file header") ;
        return EXIT_FAILURE;
        }
    if (file_pfx.subpfx_1.sdpdata_size > 0) {
        if (strncmp((const char*)&file_pfx.sdp_data[0], "v=", 2) == 0) {
                        /* uncompressed SDP string found */
            strncpy(sdpstring,(const char*)&file_pfx.sdp_data[0],file_pfx.subpfx_1.sdpdata_size);
        }
        else {
            unishox1_decompress((const char*)&file_pfx.sdp_data[0], file_pfx.subpfx_1.sdpdata_size, &sdpstring[0], NULL) ;
            //smaz_decompress((char*)&file_pfx.sdp_data[0], file_pfx.subpfx_1.sdpdata_size, &sdpstring[0], (int)sizeof(sdpstring)) ;
        }
        
        if (sdpstring[0])
        {
            std::string sdp_s = (sdpstring);
            std::istringstream iss(sdp_s);

            std::string line = "";
            while (std::getline(iss, line))
            {
                if (line.find("video") != std::string::npos ||
                        line.find("audio") != std::string::npos ||
                        line.find("rtpmap") != std::string::npos ||
                        line.find("profile-level-id=") != std::string::npos ||
                        line.find("c=IN") != std::string::npos) 
                {    
                    m_SDP += line;
                    m_SDP += '\n';
                    
                    if (line.find("audio") != std::string::npos)
                    {   
                        /*adding control in SDP: track1 for audio*/
                        m_SDP += "a=control:track1\n";
                    }
                    else if (line.find("video") != std::string::npos)
                    {
                        m_SDP += "a=control:track0\n";
                    }
                }
            }
        }
    }
    /*Now read the chunk header to fetch important info*/
    if (fread((char *)&rdhdr, sizeof(rdhdr), 1, in)  == 0) return EXIT_FAILURE;

    /*check if chunk have video*/
    if (rdhdr.streams & IS_VIDEO_STREAM_PRESENT) {
        VideoPresent = 1;
    }

    /*check if chunk have audio*/
    if (rdhdr.streams & IS_AUDIO_STREAM_PRESENT) {
        AudioPresent = 1; //audio available in
    } 
    start->tv_sec  = ntohl(rdhdr.start.tv_sec);
    start->tv_usec = ntohl(rdhdr.start.tv_usec);
    if (verbose) {
        struct tm *tm;
        struct in_addr inAddr;

        inAddr.s_addr = rdhdr.source;
        tt = (time_t)(rdhdr.start.tv_sec);
        tm = localtime(&tt);
        strftime(line, sizeof(line), "%C", tm);
        printf("Start:  %s\n", line);
        printf("Source: %s (%d)\n", inet_ntoa(inAddr), ntohs(rdhdr.port));
    }

    if (sin && sin->sin_addr.s_addr == 0) {
        sin->sin_addr.s_addr = rdhdr.source;
        sin->sin_port        = rdhdr.port;
    }
    return (EXIT_SUCCESS);
}
/*
* Read header. Return EXIT_FAILURE if not valid, 0 if ok.
*/
int RTPdumpFileReader::RD_header(FILE *in, struct sockaddr_in *sin, struct timeval *start, int verbose)
{
    RD_hdr_t hdr = {0};

    char line[80] = {0}, magic[80] = {0};

    if (fgets(line, sizeof(line), in) == NULL) return EXIT_FAILURE;
    
    sprintf(magic, "#!rtpplay%s ", RTPFILE_VERSION);
    
    if (strncmp(line, magic, strlen(magic)) != 0) return EXIT_FAILURE;
    
    if (fread((char *)&hdr, sizeof(hdr), 1, in) == 0) return EXIT_FAILURE;
    
    start->tv_sec  = ntohl(hdr.start.tv_sec);
    start->tv_usec = ntohl(hdr.start.tv_usec);
#ifdef __DEBUG
    if (verbose) {
        struct tm *tm;
        struct in_addr in;
        time_t tt;

        in.s_addr = hdr.source;
        tt = (time_t)(hdr.start.tv_sec);
        tm = localtime(&tt);
        strftime(line, sizeof(line), "%C", tm);
        printf("Start:  %s\n", line);
        printf("Source: %s (%d)\n", inet_ntoa(in), ntohs(hdr.port));
    }
#endif
    if (sin && sin->sin_addr.s_addr == 0) {
        sin->sin_addr.s_addr = hdr.source;
        sin->sin_port        = hdr.port;
    }
    return (EXIT_SUCCESS);
} /* RD_header */


/*
* Read next record from input file.
*/
int RTPdumpFileReader::RD_read(FILE *in, RD_buffer_t *b)
{
    /* read packet header from file */
        if (fread((char *)b->byte, sizeof(b->p.hdr), 1, in) == 0) {
        /* we are done */
        return (EXIT_SUCCESS);
    }
        /* convert to host byte order */
    b->p.hdr.length = ntohs(b->p.hdr.length) - sizeof(b->p.hdr);
    b->p.hdr.offset = ntohl(b->p.hdr.offset);
    b->p.hdr.plen   = ntohs(b->p.hdr.plen);
    
    /* read actual packet */
    if (fread(b->p.data, b->p.hdr.length, 1, in) == 0) {
        perror("fread body");
    }
    return b->p.hdr.length;
} /* RD_read */


bool RTPdumpFileReader::IsOpened() const
{
    return true;
}


int RTPdumpFileReader::Seek(int64_t &dts)
{

    return (EXIT_SUCCESS);
}


/**
 * Function to create a epoch time stamp from file name
 * later on this epoch could be use as filter to find the 
 * desired video data.
 *
 * @param[in] file_name - file name string to convert 
 * @param[out] epoch    - epoch time stamp created from file name
 *
 * @return    EXIT_SUCCESS - On Success 
 *            EXIT_FAILURE - On Failure
 */
int RTPdumpFileReader::toEoch(const char *file_name, long int *epoch)
{
    //TODO is there better way to do it?

    struct tm t;
    time_t    t_of_day;
    int       v_date      = 0;
    const int const_10000 = 10000;
    const int const_100   = 100;
    const int index_time  = 9;

    // parse the file name and put the values in time struct
    // then covert it to epoch time
    v_date     = atoi(file_name);
    t.tm_year  = v_date / const_10000 - EPOCH_START_YEAR;
    t.tm_mon   = (v_date % const_10000) / const_100 - EPOCH_START_MONTH;
    t.tm_mday  = v_date % const_100;
    v_date     = atoi(file_name + index_time);
    t.tm_hour  = v_date/const_10000;
    t.tm_min   = (v_date % const_10000) / const_100;
    t.tm_sec   = v_date % const_100;
    t.tm_isdst = 0;        // Is DST on? 1 = yes, 0 = no, EXIT_FAILURE = unknown
    t_of_day   = mktime(&t);
	if(t_of_day < 0)
		syslog(LOG_ERR, "incorrect epoch time... errorno = %d\n",errno);
    //printf("\n%s[%d]year = %d\n Month = %d\n Day   = %d\n Hour  = %d\n Min   = %d\n Sec   = %d", __func__,__LINE__,(int)t.tm_year, (int)t.tm_mon, (int)t.tm_mday, (int)t.tm_hour,(int)t.tm_min,(int)t.tm_sec);
    *epoch = (long int)t_of_day;

    return (EXIT_SUCCESS);
}


int RTPdumpFileReader::sortChunks(video_chunk_t * data)
{
    char *file_name = NULL;
    long epoch_time[MAX_FILE_COUNT]={0};
    chunk_t temp_chunk;
    int64_t pos = 0;

    // converting file name to epoch for sorting
    for (int i = 0; i < data->total_chunk; ++i)
    {
        file_name = strrchr(data->chunck_list[i].path, '/') + 1;
        toEoch(file_name,&epoch_time[i]);  // covert file name to epoch time
    }
    // sorting the file based on time.
    for(int i=0; i < data->total_chunk; i++)
    {
        for(int j=0; j < data->total_chunk -1; j++)
        {
            if( epoch_time[j] > epoch_time[j+1] )
            {
                int temp = epoch_time[j];
                epoch_time[j] = epoch_time[j+1];
                epoch_time[j+1] = temp;

                memcpy(&temp_chunk,&(data->chunck_list[j]),sizeof(chunk_t));
                memcpy(&(data->chunck_list[j]),&(data->chunck_list[j+1]),sizeof(chunk_t));
                memcpy(&(data->chunck_list[j+1]),&temp_chunk,sizeof(chunk_t));
            }
        }
    }
    for(int i=0; i < data->total_chunk; i++)
    {
        data->chunck_list[i].position = pos;
        pos = pos + (int64_t)data->chunck_list[i].duration;
#ifdef __DEBUG
        fprintf(stderr, "%s: %s %ld \n", __func__,data->chunck_list[i].path,data->chunck_list[i].position);
#endif    
    }
    return (EXIT_SUCCESS);
}
