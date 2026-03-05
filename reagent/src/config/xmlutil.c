#include <expat.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>

#include "xmlutil.h"

static char xmlbuf[XML_BUFF_SIZE];

static void reset_char_data_buffer();
static void process_char_data_buffer();

static int grab_next_value;

static struct cfg_file *cur_cfg;
static struct cfg_param *cur_param;

static int cur_level;
static int lev_offset, cur_offset;

static char char_data_buffer[128];
static size_t offs;
static int overflow;

int param_match(int level, const char *element, struct cfg_param *params, int nparams)
{
	int i;
	int rc = 0;

	if (params) {
		for (i = 0; i < nparams; i++) {
			if (strcmp(element, params->tag.name) == 0) {
				if (level == params->depth) {
					cur_param = params;
					rc = 1;
					break;
				}
			}
			++params;
		}
	}

	return rc;
}

void start_element(void *data, const char *element, const char **attribute)
{
	int match ;

	process_char_data_buffer();

	reset_char_data_buffer();

	if ((match=param_match(cur_level, element, cur_cfg->params, cur_cfg->nparams))) {
		if (cur_param->tag.value) {
			grab_next_value = 1;
		}
	}
#if 0
printf(">>> %s: %s/%d\n", (match)? "match" : "No match", element, cur_level) ;
#endif
	++cur_level;
}

void end_element(void *data, const char *el)
{
	int match ;

	process_char_data_buffer();

	--cur_level;

	if ((match=param_match(cur_level, el, cur_cfg->params, cur_cfg->nparams))) {
		if ((strcmp(el,"camera")==0) /*cur_param->tag.value == NULL*/) {
			cur_offset += cur_param->tag.max_val_len;
		}
	}

#if 0
	printf("<<< %s: %s/%d, cur=%s, cur_off=%d\n",
			(match)? "match" : "No match", el, cur_level, cur_param->tag.name, cur_offset) ;
#endif
	reset_char_data_buffer();
}

static void reset_char_data_buffer(void)
{
	offs = 0;
	overflow = 0;
	grab_next_value = 0;
}

// pastes parts of the node together
static void char_data(void *userData, const XML_Char *s, int len)
{
	if ((grab_next_value) && (!overflow)) {
		if (len + offs >= sizeof(char_data_buffer)) {
			overflow = 1;
		} else {
			if (offs > 0) {
				strncpy(char_data_buffer + offs, s, len);
				offs += len;
				char_data_buffer[offs] = '\0';
			} else {	// skip leading white space ...
				int i;

				strncpy(&char_data_buffer[offs], s, len);
				offs += len;
				char_data_buffer[offs] = '\0';

			}
		}
	}
}

static char *trim(char *psrc)
{
	char *p = psrc;
	char *q = psrc + strlen(psrc);

	if (p != q) {
		while ((*p & 0x000000ff) <= ' ')
			++p;
		while ((*(q - 1) & 0x000000ff) <= ' ')
			--q;
		*q = '\0';
	}
	return p;
}

static void process_char_data_buffer()
{
	int arr_off = 0 ;

	if (grab_next_value) {
		if (cur_param->is_per_camera != 0) {
			arr_off = cur_offset ;
		}
		if (offs > 0) {
			char_data_buffer[offs] = '\0';

			if (cur_param->tag.value) {
				strncpy(cur_param->tag.value + arr_off /*cur_offset*/, trim(char_data_buffer), cur_param->tag.max_val_len - 1);
			}
		} else {
			if (cur_param->tag.value) {
				(cur_param->tag.value + arr_off)[0] = '\0';
			}
		}
	}
}

static void clean_param_data(struct cfg_param *p, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if (p->tag.value) {
			*(p->tag.value) = '\0';
		}
	}
}

static int do_file(struct cfg_file *pcfg)
{
	int docfd = 0;
	int rc = 0;

	if ((docfd = open(pcfg->filename, 0)) != -1) {

		cur_level = 0;
		lev_offset = 0;
		cur_offset = 0;

		cur_cfg = pcfg;

		XML_Parser parser = XML_ParserCreate(NULL);

		XML_SetElementHandler(parser, start_element, end_element);
		XML_SetCharacterDataHandler(parser, char_data);

		clean_param_data(pcfg->params, pcfg->nparams);
		reset_char_data_buffer();

		for (;;)
		{
			int bytes_read = read(docfd, xmlbuf, XML_BUFF_SIZE);

			if (bytes_read < 0)
			{
				syslog(LOG_WARNING, "%s: *** Can't read (%s) errno (%d) at line (%d)\n", __func__, pcfg->filename, errno, __LINE__);
				rc = -1;
				break;
			}

			if (!XML_Parse(parser, xmlbuf, bytes_read, bytes_read == 0)) // bytes_read should be >= 0
			{
				syslog(LOG_WARNING, "%s: *** Can't parse %s\n", __func__, pcfg->filename);
				rc = -1;
			}

			if (bytes_read == 0)
			{
				break;
			}
		}

		XML_ParserFree(parser);

		close(docfd);
	}
	else
	{
		printf("*** Can't open %s\n", pcfg->filename);
		rc = -1;
	}
	return rc;
}

int get_cfg_params(struct cfg_file *pbase, int nfiles)
{
	int i;

	for (i = 0; i < nfiles; i++) {
		if (do_file(pbase) != 0)
			break ;
		++pbase;
	}

	return i ;
}
