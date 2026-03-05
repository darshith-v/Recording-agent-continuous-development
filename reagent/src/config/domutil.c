#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <syslog.h>

#include "mxml.h"

#include "domutil.h"

#define xstr(s) str(s)
#define str(s) #s

#define ARP_CACHE       "/proc/net/arp"
#define ARP_STRING_LEN  1023
#define ARP_BUFFER_LEN  (ARP_STRING_LEN + 1)

/* Format for fscanf() to read the 1st, 4th, and 6th space-delimited fields */
#define ARP_LINE_FORMAT "%" xstr(ARP_STRING_LEN) "s %*s %*s " \
                        "%" xstr(ARP_STRING_LEN) "s %*s " \
                        "%" xstr(ARP_STRING_LEN) "s"



#define ARP_FLUSH_TIME 120
#define MAX_LEN 100
#define IP_ADD_LENGTH 30
#define MAC_ADD_LENGTH 18

unsigned char clearArp = 0;

int get_ip_address_from_mac(char *mac_addr,char *ip_addr);
void format_mac(const char *mac_str, char *mac_formatted);
void replace_ip(char *str, char *new_ip);
mxml_node_t * get_configation_file(char *fn);
int update_configuration_file(mxml_node_t *pdom, char *tag, char *homePage,char *snapUrl,char *strmUrl);
int save_configuration_file(char *fn, mxml_node_t * tree);



/*** User 'domutil' API functions below ***/

/*
 * deletes all 'tag' children from the DOM node pointed by 'pdom'
 * returns the number of deleted child nodes.
 */
int du_del_children_by_tag(mxml_node_t *pdom, char *tag)
{
	mxml_node_t *pd, *ptmp ;
	int n = 0 ;

	if ((pdom != NULL) && (tag != NULL)) {
		for (pd = mxmlFindElement(pdom,pdom, (const char *) tag, NULL, NULL, MXML_DESCEND); pd != NULL;) {
			mxmlDelete(pd) ;
			pd = mxmlFindElement(pdom,pdom, (const char *) tag, NULL, NULL, MXML_DESCEND);
			++n ;
		}
	}
	return n ;
}

/*
 * re-links all child nodes of the 'old_parent' DOM to the 'new_parent' DOM child node list.
 * Note that the links to the adopted nodes are removed from the 'old_parent' DOM
 * returns the number of child nodes affected
 */
int du_adopt_child_nodes(mxml_node_t *pnew_parent, mxml_node_t *pold_parent)
{
	int n = 0 ;
	mxml_node_t *p_to, *p_from ;

	if ((pnew_parent != NULL) && (pold_parent != NULL)) {
		p_to = mxmlFindElement(pnew_parent, pnew_parent, "recorder", NULL, NULL, MXML_DESCEND) ;
		if (p_to != NULL) {

			for (p_from = mxmlFindElement(pold_parent, pold_parent, "camera", NULL, NULL, MXML_DESCEND); p_from != NULL;) {
				mxmlAdd(p_to, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, p_from) ;
				p_from = mxmlFindElement(pold_parent, pold_parent, "camera", NULL, NULL, MXML_DESCEND);
				++n ;
			}
		}
	}

	return n ;
}

/*
 * finds a child node specified by 'tag' and, if found
 * compares its current data value to a 'new_val'.
 * If different, sets node's data to a new value
 *
 * returns 1 if the value was changed, 0 otherwise
 */
int du_set_child_new_data(mxml_node_t *pdom, char *tag, char *new_val)
{
	int is_dirty = 0 ;
	mxml_node_t *pnode ;
	const char *cur_value ;

	pnode = mxmlFindElement(pdom, pdom, (const char *) tag, NULL, NULL, MXML_DESCEND);
	if (pnode) {
		cur_value = mxmlGetOpaque(pnode);
		if (strcmp(new_val, cur_value) != 0) {
			syslog(LOG_NOTICE,"%s:<%s> changed. Current value %s new value %s",__func__,mxmlGetElement(pnode),cur_value,new_val);
			mxmlSetOpaque(pnode, (const char *)new_val) ;
			is_dirty = 1 ;
		}
	}

	return is_dirty ;
}

////////////////////////////////////////////////////////////////////////////

static const char *usercam_tpl =
	"\n <camera>\n"
	"   <name>%s</name>\n"
	"   <camera_essential>%d</camera_essential>\n"
    "   <camera_max_recording_age>%d</camera_max_recording_age>\n"
	"   <resolution>%d</resolution>\n"
	"   <compression>%d</compression>\n"
	"   <frame_rate>%d</frame_rate>\n"
	"   <record_audio>%d</record_audio>\n"
	"   <auto_record_to_chm>0</auto_record_to_chm>\n"
	"   <auto_record_to_rssd>0</auto_record_to_rssd>\n"
	"   <mount_type>N</mount_type>\n"
	" </camera>\n"	;

static int count_user_camdefs(camera_list_element_t *ptab, int maxcamdefs)
{
	int ncams = 0 ;

	for (ncams=0; ncams < maxcamdefs; ncams++) {
		if (ptab[ncams].camera[0] == '\0') {
			break ;
		}
	}

	return ncams ;
}

static int user_camdefs_to_xml(int ncams, camera_list_element_t *ptab, const char *ptpl, char *pbuf, int maxbuf)
{
	int pos = 0 ;
	int i ;
	char tmp[16] = {0};

	if ((ptab == NULL) || (ptpl == NULL) || (pbuf == NULL))
	{
		syslog(LOG_ERR, "%s: Invalid parameters.", __func__);
		return pos ;		
	}

	memset(pbuf, 0, maxbuf) ;

	pos += snprintf(pbuf+pos, maxbuf-pos-1, "<recorder>\n") ;

	for (i=0; i < ncams; i++) 
	{
		// If the camera length is 12 characters and the camera storage is defined as 12 characters, then the camera name
		// cannot be added to the snprintf below. Hence the additional step of copying the camera name to a temp variable.
		memset(tmp, 0, sizeof(tmp));
		strncpy(tmp, ptab[i].camera, CAMERA_NAME_LENGTH);
		pos += snprintf(pbuf+pos, maxbuf-pos-1, ptpl,
			tmp, ptab[i].is_essential,ptab[i].camera_max_recording_hours,
			ptab[i].resolution, ptab[i].compression, ptab[i].frame_rate, ptab[i].audio_enable) ;
	}
	pos += snprintf(pbuf+pos, maxbuf-pos-1, "</recorder>\n") ;
#ifdef DEBUG	
	syslog(LOG_INFO, "%s: Recorder string %s", __func__, pbuf);
#endif
	return pos ;
}

static mxml_node_t * make_usercam_dom(int n_user_camdefs, camera_list_element_t *ptab, const char *ptpl)
{
	mxml_node_t *p_userdom = NULL ;

	if (n_user_camdefs > 0) {
		char *xmlbuf ;
		int xmlbuf_size ;
		int xml_length ;

		/* estimate the size of xml buffer */
		xmlbuf_size = n_user_camdefs * strlen(usercam_tpl) + n_user_camdefs * (CAMERA_NAME_LENGTH + 1) + 64 ;
		////fprintf(stderr, "Estimated XML buffer size for %d cameras = %d\n", n_user_camdefs, xmlbuf_size) ;

		if ((xmlbuf=calloc(1,xmlbuf_size)) != NULL) {
			xml_length = user_camdefs_to_xml(n_user_camdefs, ptab, usercam_tpl, xmlbuf, xmlbuf_size) ;
			////fprintf(stderr, "XML_length=%d\n------\n%s\n\n", xml_length, xmlbuf) ;

			p_userdom = mxmlLoadString(NULL, xmlbuf, MXML_OPAQUE_CALLBACK);

			if (p_userdom == NULL) {
				fprintf(stderr, "Cannot parse USER_CAM XML string\n") ;
				fprintf(stderr, "========\n%s======\n", xmlbuf) ;
			}
			free(xmlbuf); //moved this here as xmlbuf is used in the if-block above
		}
		else {
			fprintf(stderr, "Can't allocate %d bytes for XML buffer\n", xmlbuf_size) ;
		}
	}

	return p_userdom ;
}


static int patch_recorder_params(mxml_node_t *pdom, char *recorder_sn, char *uc_locoid)
{
	int is_dirty = 0 ;

	if ((recorder_sn != NULL) && (recorder_sn[0] != '\0')) {
		is_dirty |= du_set_child_new_data(pdom, "serial_number", recorder_sn) ;
	}
	if ((uc_locoid != NULL) && (uc_locoid[0] != '\0')) {
		is_dirty |= du_set_child_new_data(pdom, "loco_id", uc_locoid) ;
	}

	return is_dirty ;
}


static int patch_camdefs(mxml_node_t* pdom,camera_list_element_t* uc_cameras,int n_camdef,int *n_matched)
{
	if (pdom == NULL || uc_cameras == NULL)
		return 0;

	mxml_node_t *node;
	mxml_node_t * ptmp ;
	mxml_index_t *idx ;
	int i = 0;
	int is_dirty = 0;
	int matched = 0;
	char parambuf[8] ={0};

	
	idx = mxmlIndexNew(pdom, "camera", NULL) ;
	if(idx)
	{
		node = mxmlIndexReset(idx);
		for (node = mxmlIndexEnum(idx) ; node != NULL; node = mxmlIndexEnum(idx)) {
			/* collect currently defined camera names */
			ptmp = mxmlFindElement(node, node, "name", NULL, NULL, MXML_DESCEND) ;
			if (ptmp) {
				syslog(LOG_NOTICE,"%s: Checking %s ",__func__,mxmlGetOpaque(ptmp));
				for (i=0; i<n_camdef; ++i)
				{
					if (uc_cameras[i].camera[0] != '\0' &&
					    strncasecmp(mxmlGetOpaque(ptmp),&uc_cameras[i].camera[0],CAMERA_NAME_LENGTH) == 0)
					{
						matched++;
						syslog(LOG_NOTICE,"%s: %s matched",__func__,mxmlGetOpaque(ptmp));
						sprintf(parambuf, "%d", uc_cameras[i].resolution) ;
						is_dirty |= du_set_child_new_data(node, "resolution", parambuf) ;
						sprintf(parambuf, "%d", uc_cameras[i].compression) ;
						is_dirty |= du_set_child_new_data(node, "compression", parambuf) ;
						sprintf(parambuf, "%d", uc_cameras[i].frame_rate) ;
						is_dirty |= du_set_child_new_data(node, "frame_rate", parambuf) ;
						sprintf(parambuf, "%d", uc_cameras[i].audio_enable) ;
						is_dirty |= du_set_child_new_data(node, "record_audio", parambuf) ;
						sprintf(parambuf, "%d", uc_cameras[i].is_essential) ;
						is_dirty |= du_set_child_new_data(node, "camera_essential", parambuf) ;
						sprintf(parambuf, "%d", uc_cameras[i].camera_max_recording_hours) ;
						is_dirty |= du_set_child_new_data(node, "camera_max_recording_age", parambuf) ;
						if(is_dirty)
							syslog(LOG_NOTICE,"%s: %s attribute(s) dirty ",__func__,mxmlGetOpaque(ptmp));
						break;
					}//if (uc_cameras[i].camera[0] != '\0' &&
				} //for (i=0; i<n_camdef; ++i)
			} //if (ptmp)
		} //for (node = mxmlIndexReset(idx) ; node != NULL; node = mxmlIndexEnum(idx))
		mxmlIndexDelete(idx) ;
	}
	else
	{
		is_dirty = 1;
	}
	/*All cameras are not matched?*/
	if (matched != n_camdef)
	{
		syslog(LOG_NOTICE,"*** All cameras are not matched [%d/%d]!!!",matched,n_camdef);
		is_dirty = 1;
	}

	if (n_matched != NULL)
		*n_matched = matched;

	return is_dirty;
}

static int patch_camera_params(mxml_node_t *pdom, mxml_index_t * base_cam_idx, camera_list_element_t *uc_cameras, int n_camdefs)
{
	int is_dirty = 0 ;
	int i ;
	char parambuf[8] ;
	mxml_node_t *pcamnode ;

	pcamnode = mxmlIndexEnum(base_cam_idx) ;
	for (i=0; i < n_camdefs; i++) {
		sprintf(parambuf, "%d", uc_cameras[i].resolution) ;
		is_dirty |= du_set_child_new_data(pcamnode, "resolution", parambuf) ;
		sprintf(parambuf, "%d", uc_cameras[i].compression) ;
		is_dirty |= du_set_child_new_data(pcamnode, "compression", parambuf) ;
		sprintf(parambuf, "%d", uc_cameras[i].frame_rate) ;
		is_dirty |= du_set_child_new_data(pcamnode, "frame_rate", parambuf) ;
		sprintf(parambuf, "%d", uc_cameras[i].audio_enable) ;
		is_dirty |= du_set_child_new_data(pcamnode, "record_audio", parambuf) ;
		sprintf(parambuf, "%d", uc_cameras[i].is_essential) ;
		is_dirty |= du_set_child_new_data(pcamnode, "camera_essential", parambuf) ;
		sprintf(parambuf, "%d", uc_cameras[i].camera_max_recording_hours) ;
		is_dirty |= du_set_child_new_data(pcamnode, "camera_max_recording_age", parambuf) ;

		pcamnode = mxmlIndexEnum(base_cam_idx) ;
	}

	return is_dirty ;
}

static int cp_base_to_current(char *base_config, char *current_config)
{
	int rc = 1 ;

	char *pcmd = calloc(1, (strlen(base_config)+strlen(current_config)+32)) ;
	if (pcmd) {
		sprintf( pcmd, "/bin/cp -pf \'%s\' \'%s\'", base_config, current_config);
		rc = system(pcmd) ;
		syslog(LOG_WARNING,  "*** file %s replaced\n", current_config ) ;
		free(pcmd) ;
	}

	return rc ;
}

mxml_node_t * get_config_file(char *fn)
{
	mxml_node_t * tree = NULL ;
	FILE *fp ;

	if ((fp=fopen(fn, "r")) != NULL) {
		tree = mxmlLoadFile(NULL, fp, MXML_OPAQUE_CALLBACK);
		fclose(fp) ;
	}
	else {
		syslog(LOG_WARNING,  "*** cannot open %s for reading\n", fn) ;
	}

	return tree ;
}

int save_config_file(char *fn, mxml_node_t * tree)
{
	FILE *fp = fopen(fn,"w") ;
	int rc = -1 ;

	if (fp) {
		rc = mxmlSaveFile(tree , fp, MXML_NO_CALLBACK) ;
		fclose(fp) ;
	}
	else {
		syslog(LOG_WARNING,  "*** cannot open %s for writing\n", fn) ;
	}
	
	return rc ;
}

int du_accept_user_config(char *base_config, char *recorder_sn, char *uc_locoid, camera_list_element_t *uc_cameras)
{
	int rc = 0 ;
	mxml_node_t * pdom ;
	mxml_node_t * p_userdom ;

	const char *cur_serno=NULL, *cur_locoid=NULL ;

	mxml_index_t *base_cam_idx ;	/* index(list) of currently defined cameras */
	int base_n_camdefs = 0 ;		/* number of currently defined cameras */

	int n_user_camdefs = 0 ;
	int i = 0;
	int mat_cam = 0;
	int is_dirty = 0 ;
	char *current_config = NULL ;

	char base_cam_names[NUM_CAMERAS][CAMERA_NAME_LENGTH + 2] = {{0}}; /* names of currently defined cameras */
	mxml_node_t * ptmp ;
	int do_replace_camdefs = 0 ;

	if ((base_config != NULL) && (uc_cameras != NULL)) {
		pdom = get_config_file(base_config) ;
		if (pdom) {
			current_config = calloc(1, strlen(base_config)+32) ;
			if (current_config) {
				char *p = NULL;

				strcpy(current_config, base_config) ;
				p = strrchr(current_config, '/') ;
				if (p) {
					++p ;
				}
				else {
					p = current_config ;
				}

				strcpy(p, "current_config.xml") ;
				fprintf(stderr, "*** Current: %s\n", current_config) ;
			}
			else
			{ //to capture failure of calloc and free buffer to prevent leaks.
				mxmlDelete(pdom); 
				syslog(LOG_ERR, "%s: Failure in allocating memory, at line (%d)\n", __func__, __LINE__);
				return 1;
			}

			is_dirty |= patch_recorder_params(pdom, recorder_sn, uc_locoid) ;

			n_user_camdefs = count_user_camdefs(&uc_cameras[0], NUM_CAMERAS) ;

			if (n_user_camdefs > 0) {
				p_userdom = make_usercam_dom(n_user_camdefs, &uc_cameras[0], usercam_tpl) ;
			}
			else {
				p_userdom = NULL ;
			}

			/* create an index of '<camera>' tags in base configuration */
			base_cam_idx = mxmlIndexNew(pdom, "camera", NULL) ;
			base_n_camdefs = mxmlIndexGetCount(base_cam_idx) ;
			/*
			 * do further checks if the user- and current- number of camera definitions are the same,
			 * otherwise just replace the current camera list with the user camera definitions
			 */
			if ((n_user_camdefs > 0) && (base_n_camdefs == n_user_camdefs)) {
				mat_cam = 0;
				/*
				* we will iter through uc_Cameras and camera in base configuration 
				* and we will find matching cameras with same name
				* if number of matching camera are different than base_n_camdefs we will overwrite 
				* the base_config
				* we will also update the base_config cameras attributes if those are different than 
				* uc_cameras. if there attributes are the only change then will not overwrite the whole 
				* base configuration. 
				*/
				is_dirty |= patch_camdefs(pdom,&uc_cameras[0],n_user_camdefs,&mat_cam);

				if(mat_cam != base_n_camdefs)
				{
					do_replace_camdefs = 1;
				}
			}
			else {
				do_replace_camdefs = 1 ;
			}

			if (do_replace_camdefs) {
				du_del_children_by_tag(pdom, "camera") ;
				if (p_userdom) {
					du_adopt_child_nodes(pdom, p_userdom) ;
					mxmlDelete(p_userdom) ;
					p_userdom = NULL ;
				}
				else {
					syslog(LOG_WARNING,  "Cannot parse generated USER_CAM XML string\n") ;
					n_user_camdefs = 0 ;
				}
				is_dirty |= 1 ;
			}
			mxmlIndexDelete(base_cam_idx) ;
			syslog(LOG_INFO, "%s: Num configured cameras: %d", __func__,n_user_camdefs );

			if (is_dirty) {
				rc = save_config_file(base_config, pdom) ;
				if (rc == 0) {
					printf("%s modified\n", base_config) ;
				}

				is_dirty = 0 ;
			}
			else {
				mxml_node_t *pdcur ;

				syslog(LOG_WARNING,  "*** file %s left untouched\n", base_config ) ;

				/* for extra sanity ... */
				pdcur = get_config_file(current_config) ;
				if (pdcur == NULL) {
					syslog(LOG_WARNING,  "*** Cannot parse %s -- replaced with base config\n", current_config) ;
					do_replace_camdefs = 1 ;
				}
				else {
					mxmlDelete(pdcur) ;
				}
			}

/*** done with base_config.xml, do current_config.xml now */

			if ((do_replace_camdefs != 0) && (current_config != NULL)) {
				syslog(LOG_NOTICE,"base_config change, replacing current_config");
				cp_base_to_current(base_config, current_config) ;
			}
			else if (current_config != NULL)
			{ /* message current_config.xml inplace, if necessary */
				mxml_node_t *pdcur = get_config_file(current_config);

				if (pdcur) {
					/* just a sanity check -- make sure that the number of camdefs is the same as in base_config */
					is_dirty = 0 ;

					is_dirty |= patch_recorder_params(pdcur, recorder_sn, uc_locoid) ;
					if(is_dirty)
					{
						syslog(LOG_NOTICE,"current_config: patch_recorder_params is dirty");
					}
					//is_dirty |= patch_camera_params(pdcur, idx, &uc_cameras[0], n) ;
					is_dirty |= patch_camdefs(pdcur,&uc_cameras[0],n_user_camdefs, NULL);
					if(is_dirty)
					{
						syslog(LOG_NOTICE,"current_config: patch_camdefs is dirty");
					}

					if (is_dirty) {
						rc = save_config_file(current_config, pdcur) ;
						if (rc == 0) {
							syslog(LOG_WARNING,  "*** %s modified\n", current_config) ;
						}

						is_dirty = 0 ;
					}
					else {
						syslog(LOG_WARNING,  "*** %s left untouched\n", current_config) ;
					}
					mxmlDelete(pdcur) ;
				}
				else {
					syslog(LOG_WARNING,  "*** Cannot parse %s -- replaced with base config\n", current_config) ;
					cp_base_to_current(base_config, current_config) ;
				}
			}

			mxmlDelete(pdom);
			free(current_config);
		}
		else
		{
			syslog(LOG_WARNING, "*** Cannot parse %s\n", base_config);
			rc = 1;
		}
	}

	return rc ;
}


/**
 * @brief get_ip_address_from_mac - retrieves the IP address associated with a given MAC address
 *
 * @mac_addr: MAC address as a string
 * @ip_addr: buffer to store the found IP address
 *
 * Returns: 1 if the IP address is found successfully, -1 if not found
 */
int get_ip_address_from_mac(char *mac_addr, char *ip_addr)
{
    int rc = 0;
    FILE *arpCache = fopen(ARP_CACHE, "r");
    char mac_formatted[MAC_ADD_LENGTH];
    // Flush the ARP table
    if (clearArp == 0)
    {
        clearArp++;
        clearArp %= ARP_FLUSH_TIME;
        rc = system("ip -s -s neigh flush all");

        if (rc != EXIT_SUCCESS)
        {
            syslog(LOG_ERR, "%s: Error flushing the ARP table", __func__);
			if (arpCache != NULL)
			{
				fclose(arpCache);
			}
			return -1;
        }
    }

    if (!arpCache)
    {
        perror("Arp Cache: Failed to open file \"" ARP_CACHE "\"");
        return -1;
    }

    /* Ignore the first line, which contains the header */
    char header[ARP_BUFFER_LEN];
    if (!fgets(header, sizeof(header), arpCache))
    {
        fclose(arpCache);
        return -1;
    }

    char ipAddr[ARP_BUFFER_LEN], hwAddr[ARP_BUFFER_LEN], device[ARP_BUFFER_LEN];
    int count = 0;

    while (3 == fscanf(arpCache, ARP_LINE_FORMAT, ipAddr, hwAddr, device))
    {
        memset(mac_formatted, 0, sizeof(mac_formatted));
        format_mac(mac_addr, mac_formatted);

        if (strcmp(mac_formatted, hwAddr) == 0)
        {
            fclose(arpCache);
            memset(ip_addr, 0, IP_ADD_LENGTH);
            memcpy(ip_addr, ipAddr, IP_ADD_LENGTH);//Ensuring null-termination of buffers.
            return 1;
        }
    }
    fclose(arpCache);
    return -1;
}

/**
 * @brief Formats a MAC address by adding colons after every two characters and converting all characters to lower case
 *
 * @param[in] mac_str A constant string that represents an unformatted MAC address
 * @param[out] mac_formatted A buffer to store the formatted MAC address
 *
 * @return None
 */
void format_mac(const char *mac_str, char *mac_formatted)
{
    int i;
    for (i = 0; i < strlen(mac_str); i++)
    {
        char c = tolower(mac_str[i]);
        if (i % 2 == 0 && i > 0)
        {
            strncat(mac_formatted, ":", 1);
        }
        strncat(mac_formatted, &c, 1);
    }
}

/**
 * @brief Replaces the string with the given IP address
 *
 * @param[in,out] str The input string in which the IP address needs to be replaced
 * @param[in] new_ip The new IP address to be replaced in the input string
 * @return None
 */

void replace_ip(char *str, char *new_ip)
{
    char *start, *end;
    char buffer[MAX_LEN];

    start = strstr(str, "http://");
    if (start == NULL)
    {
        start = strstr(str, "rtsp://");
        if (start == NULL)
        {
            return;
        }
    }
    start += strlen("http://");
    end = strchr(start, ':');
    if (end == NULL)
    {
        end = strchr(start, '/');
    }
    if (end == NULL)
    {
        return;
    }

    int length = end - start;
    strncpy(buffer, start, length);
    buffer[length] = '\0';

    char *result = malloc(strlen(str) + strlen(new_ip) - length + 1);
    if (result == NULL)
    {
        syslog(LOG_NOTICE, "%s , %d,Error: Failed to allocate memory for result string", __func__, __LINE__);
        return;
    }
    strncpy(result, str, start - str);
    result[start - str] = '\0';
    strcat(result, new_ip);
    strcat(result, end);

    strcpy(str, result);
    free(result);
}

/**
 * @brief Opens the configuration file and return mxml_node_t
 *
 * @param[in] fn The input string of the configuration file
 * @return mxml_node_t
 */
mxml_node_t *get_configation_file(char *fn)
{
    mxml_node_t *tree = NULL;
    FILE *fp;

    if ((fp = fopen(fn, "r")) != NULL)
    {
        tree = mxmlLoadFile(NULL, fp, MXML_TEXT_CALLBACK);
        fclose(fp);
    }
    else
    {
        fprintf(stdout, "*** cannot open %s for reading\n", fn);
    }

    return tree;
}

/**
 * @brief Updates the configuration file with the given inputs
 *
 * @param[in] pdom mxml_node_t
 * @param[in] tag  The camera name
 * @param[in] homePage The homepage of the camera
 * @param[in] snapUrl Snapshot url of the camera
 * @param[in] strmUrl Strean url of the camera
 * @return int
 */
int update_configuration_file(mxml_node_t *pdom, char *tag, char *homePage, char *snapUrl, char *strmUrl)
{
    int is_dirty = 0;
    mxml_node_t *pnode;
    mxml_node_t *childNode;
    const char *currElement;
    const char *currValue;
    char prevElement[MAX_LEN] = {0};
    const char *camStr = "camera";
    int count = 1;
    char *name = NULL;
    unsigned char ucCameraTreeFound = 0;
    for (pnode = mxmlFindElement(pdom, pdom, NULL, NULL, NULL, MXML_DESCEND);
         pnode != NULL;
         pnode = mxmlWalkNext(pnode, NULL, MXML_DESCEND)
         // node = mxmlFindElement(node, tree, NULL,NULL,NULL,MXML_DESCEND)

    )
    {
        if (mxmlGetType(pnode) == MXML_ELEMENT)
        {
            currElement = mxmlGetElement(pnode);
            currValue = mxmlGetText(pnode, 0);
            // fprintf(stdout,"MXML_ELEMENT Node <%s>:Value:%s \n", currElement,currValue);

            if (strcmp(currElement, camStr) == 0)
            {
                strcpy(prevElement, "camera");
            }

            if (strcmp(currElement, "name") == 0 && (strcmp("camera", prevElement) == 0))
            {
                // Check for the Current value
                if (strcmp(currValue, tag) == 0)
                {
                    ucCameraTreeFound = 1;
                }
            }

            if (ucCameraTreeFound == 1)
            {

                // printf("currElement:%s homePage:%s",currElement,homePage);
                if (strcmp(currElement, "home_page") == 0)
                {
                    if (strcmp(homePage, currValue) != 0)
                    {
                        fprintf(stdout, "%s:<%s> changed. Current value %s new value %s", __func__, mxmlGetElement(pnode), currValue, homePage);
                        mxmlSetText(pnode, 0, (const char *)homePage);
                    }
                    else
                    {
                        fprintf(stdout, "\n %s:: New values are same::Current value %s new value %s", __func__, currValue, homePage);
                    }
                }

                if (strcmp(currElement, "snapshot_url") == 0)
                {
                    if (strcmp(snapUrl, currValue) != 0)
                    {
                        fprintf(stdout, "%s:<%s> changed. Current value %s new value %s", __func__, mxmlGetElement(pnode), currValue, snapUrl);
                        mxmlSetText(pnode, 0, (const char *)snapUrl);
                    }
                    else
                    {
                        fprintf(stdout, "\n %s:: New values are same::Current value %s new value %s", __func__, currValue, snapUrl);
                    }
                }

                if (strcmp(currElement, "stream_url") == 0)
                {
                    ucCameraTreeFound = 0;
                    if (strcmp(strmUrl, currValue) != 0)
                    {
                        fprintf(stdout, "%s:<%s> changed. Current value %s new value %s", __func__, mxmlGetElement(pnode), currValue, strmUrl);
                        mxmlSetText(pnode, 0, (const char *)strmUrl);
                        is_dirty = 1;
                    }
                    else
                    {
                        fprintf(stdout, "\n %s:: New values are same::Current value %s new value %s\n", __func__, currValue, strmUrl);
                    }
                }
            }
        }
    }

    return is_dirty;
}

/**
 * @brief Saves the configutaion file
 *
 * @param[in] fn The input string of the configuration file
 * @param[in] tree mxml_node_t node
 * @return int
 */
int save_configuration_file(char *fn, mxml_node_t *tree)
{
    FILE *fp = fopen(fn, "w");
    int rc = -1;

    if (fp)
    {
        rc = mxmlSaveFile(tree, fp, MXML_NO_CALLBACK);
        fclose(fp);
    }
    else
    {
        fprintf(stdout, "*** cannot open %s for writing\n", fn);
    }

    return rc;
}
