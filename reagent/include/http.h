#ifndef _HTTP_H_
#define _HTTP_H_

#ifndef USE_LUA
#define USE_LUA 1
#endif

#ifndef USE_DUKTAPE
#define USE_DUKTAPE 1
#endif

#define MG_EXPERIMENTAL_INTERFACES 1

#ifdef DAEMONIZE
#undef DAEMONIZE
#endif

#ifdef CONFIG_FILE
#undef CONFIG_FILE
#endif
#ifdef CONFIG_FILE2
#undef CONFIG_FILE2
#endif
#ifdef PASSWORDS_FILE_NAME
#undef PASSWORDS_FILE_NAME
#endif

#define CONFIG_FILE "/opt/reagent/etc/http.conf"
////#define CONFIG_FILE2 "/opt/reagent/etc/http_alt.conf"
#define PASSWORDS_FILE_NAME "/opt/reagent/etc/.htpasswd"

extern volatile int g_exit_flag ;

void * http_start(int argc, char *argv[]) ;
int http_stop(int sig_num) ;
#endif
