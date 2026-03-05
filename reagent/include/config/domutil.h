#ifndef _DOMUTIL_H_
#define _DOMUTIL_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "video_api.h"
#include "mxml.h"

extern int du_set_child_new_data(mxml_node_t *pdom, char *tag, char *new_val) ;
extern int du_del_children_by_tag(mxml_node_t *pdom, char *tag) ;
extern int du_adopt_child_nodes(mxml_node_t *pnew_parent, mxml_node_t *pold_parent) ;
extern int du_accept_user_config(char *base_config, char *recorder_sn, char *uc_locoid, camera_list_element_t *uc_cameras) ;
extern mxml_node_t * get_config_file(char *fn);
extern int save_config_file(char *fn, mxml_node_t * tree);

extern int get_ip_address_from_mac(char *mac_addr,char *ip_addr);
extern void replace_ip(char *str, char *new_ip);
extern mxml_node_t * get_configation_file(char *fn);
extern int update_configuration_file(mxml_node_t *pdom, char *tag, char *homePage,char *snapUrl,char *strmUrl);
extern int save_configuration_file(char *fn, mxml_node_t * tree);
extern unsigned char clearArp;

#endif
