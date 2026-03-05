ifndef RA_TARGET
   $(error RA_TARGET is not set)
endif

ifndef RA_BUILD_DIR
   RA_BUILD_DIR := $(RA_TARGET)
   $(warning RA_BUILD_DIR is not set, setting to $(RA_BUILD_DIR)) 
endif

BUILD_SPEC ?= release

ifneq ($(BUILD_SPEC),debug)
   OBJDIR := $(RA_BUILD_DIR)/obj
 ##  LIBDIR := $(RA_BUILD_DIR)/lib
   BINDIR := $(RA_BUILD_DIR)/bin
   CFLAGS = -O3 -g0 -fPIC
else   
   OBJDIR := $(RA_BUILD_DIR)/objdebug
##   LIBDIR := $(RA_BUILD_DIR)/libdebug
   BINDIR := $(RA_BUILD_DIR)/bindebug
   CFLAGS = -O0 -g3 -fPIC
endif

CC    = $(CROSS_PFX)gcc$(CROSS_SFX)
CXX   = $(CROSS_PFX)g++$(CROSS_SFX)
AR    = $(CROSS_PFX)ar$(CROSS_SFX)
AS    = $(CROSS_PFX)gcc$(CROSS_SFX)
CPP   = $(CROSS_PFX)gcc$(CROSS_SFX) -E
LD    = $(CROSS_PFX)ld$(CROSS_SFX)
NM    = $(CROSS_PFX)nm$(CROSS_SFX)
STRIP = $(CROSS_PFX)strip$(CROSS_SFX)

MKDIR		= mkdir -p

ifeq ($(findstring nto, $(CROSS_PFX)),nto)
   CFLAGS += -DQNX_BUILD=1
endif

###################################
BITNESS ?=
PRJ_DIR        = .
PROG	= access_data

SRCDIR = src ../common/src 
INCDIR := include ../common/include \
  ../reagent/include ../reagent/include/config ../reagent/include/video \
  ../reagent/include/http \
  ../reagent/include/expat ../reagent/include/camclient  \
  ../common/include/ptc/ft-linux-12353/include ../common/include/ptc/lib_ptc_api/hdr \
  ../common/include/ptc/lib_ptc_sys/hdr ../common/include/ptc/lib_ptc_utils/hdr \
  ../common/include/video \
  ../reagent/include/cJSON include/video_proc\


# Set search paths for source files
# Add any subdirs here
#------------------------------------
ADDITIONALSOURCE = ../common/src ../reagent/src ../reagent/src/expat ../reagent/src/config ../reagent/src/camclient ../reagent/src/cJSON ../reagent/src/video_proc src/vra_utils

MKINCLUDES =

BUILD_SPEC     = default

INCLUDES = $(patsubst %,-I%,$(subst :, ,$(INCDIR)))

CFLAGS += -DOS_TGT_LINUX -DHAVE_MEMMOVE -DHAVE_BCOPY

-include $(MKINCLUDES)

CSOURCES =  \
	access_data.c \
	reagent_config.c \
	user_config.c \
	xmlutil.c \
	xmlparse.c \
	xmlrole.c \
	xmltok.c \
	ra_data_search.c \
	coc.c \
	ra_download.c \
	ra_util.c \
	groom_util.c \
	shared_memory.c \
	dir_walker.c \
	camon.c \

CXXSOURCES := \
		
OBJS= $(CSOURCES:.c=.o) $(CXXSOURCES:.cpp=.o)
		
OBJ_FILES = $(patsubst %,$(OBJDIR)/%,$(subst :, ,$(OBJS)))
		
INCLUDE_FILES = \

#Directory Search Paths
vpath %.cpp $(SRCDIR) $(ADDITIONALSOURCE)
vpath %.c $(SRCDIR) $(ADDITIONALSOURCE)
vpath %.h $(INCDIR)

LIBDIR = ../$(RA_BUILD_DIR)/lib
COMLIBDIR = ../common/$(RA_BUILD_DIR)/lib
DEPLIBDIR = ../depend/$(RA_BUILD_DIR)/lib
LDFLAGS = -L$(LIBDIR) -L$(COMLIBDIR) -L$(DEPLIBDIR) -lmxml -lovconfig -lonvifcli -L$(LIBDIR) -lptcapi -lptcsys -lptcutils -lcivetweb -llua -lm -lz -lssl -lcrypto

ifneq ($(findstring nto, $(CROSS_PFX)),nto)
   CFLAGS += -pthread -D_REENTRANT
   LDFLAGS += -pthread
   LDFLAGS += -lrt -lpthread -ldl
else
   LDFLAGS += -lsocket
endif

CFLAGS += $(MFLAG)
LDFLAGS += $(MFLAG)

COPT ?= -DLINUX -Iinclude -DNO_CACHING -DMG_EXPERIMENTAL_INTERFACES -DUSE_STACK_SIZE=102400 -O2 -DNDEBUG -DLUA_COMPAT_5_2 \
-DLUA_VERSION_MAKEFILE=503 -DLUA_COMPAT_ALL -DUSE_LUA -DLUA_USE_POSIX -DLUA_USE_DLOPEN -DTHREADSAFE=1 -DSQLITE_ENABLE_FTS3 -DSQLITE_ENABLE_FTS3_PARENTHESIS \
-DUSE_LUA_SQLITE3 -DUSE_LUA_FILE_SYSTEM -DUSE_LUA_LUAXML -DUSE_LUA_LUAJSC -DDUKTAPE_VERSION_MAKEFILE=203 -DUSE_DUKTAPE -DUSE_ZLIB \
-DMG_EXPERIMENTAL_INTERFACES -DUSE_WEBSOCKET -DUSE_SERVER_STATS -DDAEMONIZE -DSSL_LIB=\"libssl.so.1.1\" -DCRYPTO_LIB=\"libcrypto.so.1.1\" -DOPENSSL_API_1_1 -DPID_FILE=\"/var/run/civetweb.pid\" -fPIC


# Generic Rules
${OBJDIR}/%.o: %.c
	$(CC) -c $(CDEFINES) $(CFLAGS) $(COPT) $(INCLUDES) -o $@ $<

${OBJDIR}/%.o: %.cpp
	$(CXX) -c $(CDEFINES) $(CFLAGS) $(COPT) $(INCLUDES) -o $@ $<

all:	$(PROG) $(INCLUDE_FILES)	

# bin directory 
# creates the bin directory if it doesn't exist
#------------------------------------
$(OBJDIR):
	$(MKDIR) $(OBJDIR) 

$(BINDIR):
	$(MKDIR) $(BINDIR) 

-include $(OBJDIR)/*.d

$(PROG): $(OBJDIR) $(BINDIR) Makefile $(LDSCRIPT) $(OBJ_FILES)
	@echo ======================  Link  ================================
	$(CC) $(COPT) -o ${BINDIR}/$(PROG) $(OBJ_FILES) $(LDFLAGS)
	rm -f ./$(PROG)
	cp ${BINDIR}/$(PROG) .

clean:
	rm -rf $(OBJDIR)/*.o $(OBJDIR)/*.d 
	rm -rf $(BINDIR)/*
	rm -rf ./$(PROG)
	rm -rf $(RA_BUILD_DIR)
