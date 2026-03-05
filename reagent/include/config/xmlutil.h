#ifndef _XMLUTIL_H_
#define _XMLUTIL_H_

#define XML_BUFF_SIZE 4096
#define CFG_PARAM_SIZE 16

typedef struct nameval_s {
	char *name ;
	char *value ;
	int max_val_len ;
} nameval_t ;

typedef struct cfg_param {
	int depth ;					/* xml tag depth */
	int is_per_camera ;			/* is it a per-camera parameter ? */
	nameval_t tag ;				/* XML tag of interest */
	nameval_t *attr_list ;		/* ptr to its attribute list, if any */
	int n_attr ;				/* number of attributes in the list above */
} cfg_param_t ;

typedef struct cfg_file {
	char *filename ;
	struct cfg_param *params ;
	int nparams ;
} cfg_file_t ;

int get_cfg_params(cfg_file_t *pbase, int nfiles) ;

#endif
