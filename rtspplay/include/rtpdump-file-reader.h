#ifndef _RTPdump_file_reader_h_
#define _RTPdump_file_reader_h_

#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "rtpdump.h"
#include "vradump.h"


class RTPdumpFileReader
{
public:
    RTPdumpFileReader(const std::string dir, const std::string file_type);
    ~RTPdumpFileReader();

    bool IsOpened() const;

public:
    int GetDuration(int64_t& duration) const { duration = m_duration; return 0; }
    int Seek(int64_t &dts);
    int readNextPacket(RD_buffer_t *b);
    std::string GetSDP() const { return m_SDP; };
    int resetReader(void);
    int setSeekPosition(int64_t position);

    int AudioPresent;
    int VideoPresent;
private:
    int openChunk();
    int read_dir(video_chunk_t *data, const char *in_dir, const char *pat);
    int RD_header(FILE *in, struct sockaddr_in *sin, struct timeval *start, int verbose);
    int VRA_header(FILE *in, struct sockaddr_in *sin, struct timeval *start, int verbose);
    int RD_read(FILE *in, RD_buffer_t *b);
    int smaz_decompress(char *in, int inlen, char *out, int outlen);
    int toEoch(const char *file_name, long int *epoch);
    int sortChunks(video_chunk_t * data);
private:
    FILE* m_activeFile;
    video_chunk_t m_videoData;

    uint16_t m_index;
    int64_t m_duration;
    uint8_t *m_ptr;
    size_t m_capacity;
    uint8_t m_verbose;
    std::string m_SDP;

};

extern "C" {

static std::string Smaz_rcb[] = {
" ", "the", "e", "t", "a", "of", "o", "and", "i", "n", "s", "e ", "r", " th",
" t", "in", "he", "th", "h", "he ", "to", "\r\n", "l", "s ", "d", " a", "an",
"er", "c", " o", "d ", "on", " of", "re", "of ", "t ", ", ", "is", "u", "at",
"   ", "n ", "or", "which", "f", "m", "as", "it", "that", "\n", "was", "en",
"  ", " w", "es", " an", " i", "\r", "f ", "g", "p", "nd", " s", "nd ", "ed ",
"w", "ed", "http://", "for", "te", "ing", "y ", "The", " c", "ti", "r ", "his",
"st", " in", "ar", "nt", ",", " to", "y", "ng", " h", "with", "le", "al", "to ",
"b", "ou", "be", "were", " b", "se", "o ", "ent", "ha", "ng ", "their", "\"",
"hi", "from", " f", "in ", "de", "ion", "me", "v", ".", "ve", "all", "re ",
"ri", "ro", "is ", "co", "f t", "are", "ea", ". ", "her", " m", "er ", " p",
"es ", "by", "they", "di", "ra", "ic", "not", "s, ", "d t", "at ", "ce", "la",
"h ", "ne", "as ", "tio", "on ", "n t", "io", "we", " a ", "om", ", a", "s o",
"ur", "li", "ll", "ch", "had", "this", "e t", "g ", "e\r\n", " wh", "ere",
" co", "e o", "a ", "us", " d", "ss", "\n\r\n", "\r\n\r", "=\"", " be", " e",
"s a", "ma", "one", "t t", "or ", "but", "el", "so", "l ", "e s", "s,", "no",
"ter", " wa", "iv", "ho", "e a", " r", "hat", "s t", "ns", "ch ", "wh", "tr",
"ut", "/", "have", "ly ", "ta", " ha", " on", "tha", "-", " l", "ati", "en ",
"pe", " re", "there", "ass", "si", " fo", "wa", "ec", "our", "who", "its", "z",
"fo", "rs", ">", "ot", "un", "<", "im", "th ", "nc", "ate", "><", "ver", "ad",
" we", "ly", "ee", " n", "id", " cl", "ac", "il", "</", "rt", " wi", "div",
"e, ", " it", "whi", " ma", "ge", "x", "e c", "men", ".com"
};
}
#endif /* !_RTPdump_file_reader_h_ */
