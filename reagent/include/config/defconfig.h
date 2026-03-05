#ifndef _DEFCONFIG_H_
#define _DEFCONFIG_H_


typedef struct
{
	int 	type ; 	/* base type */
	int 	size ;  /* in bytes */
	int		count ;	/* number of elements */
	int 	flags ;	/* misc flags */
	int		(*getfun)(void *, char *, int, int) ; /* converter to internal form */
	void	(*putfun)(char *, void *, char *, int, int) ; /* converter to external form */
	int		(*hw_peek)(char *, void *, int, int) ; /* hw register peek handler */
	int		(*hw_poke)(char *, void *, int, int) ; /* hw register poke handler */
	void 	* def_value ;	/* factory default value, internal form */
	void 	* cur_value ;	/* current value, internal form */
	void 	* enum_restrictions ;	/* pointer to an array of enum restrictions, if any */
	int 	nenums ;
	void 	* range_restrictions ;	/* pointer to an array of range restrictions, if any */
	int 	nranges ;
} config_info ;

typedef struct
{
	 void * scew_el ;
	 config_info *pconf_info ;
} scelement_wrapper ;

int GetNoElements();

#endif
