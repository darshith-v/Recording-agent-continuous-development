ifndef RA_TARGET
   $(error RA_TARGET is not set)
endif

ifndef RA_BUILD_DIR
   RA_BUILD_DIR := $(RA_TARGET)
   $(warning RA_BUILD_DIR is not set, setting to $(RA_BUILD_DIR)) 
endif

BUILD_SPEC ?= release

ifeq ($(BUILD_SPEC),release)
   OBJDIR := $(RA_BUILD_DIR)/obj
 ##  LIBDIR := $(RA_BUILD_DIR)/lib
   BINDIR := $(RA_BUILD_DIR)/bin
   CFLAGS = -O3 -g0 -fPIC
else   
   OBJDIR := $(RA_BUILD_DIR)/objdebug
##   LIBDIR := $(RA_BUILD_DIR)/libdebug
   BINDIR := $(RA_BUILD_DIR)/bindebug
   CFLAGS = -O0 -g3 -fPIC -D__DEBUG
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

PROG	= rtspplay
PRJ_DIR = .
DEPENDENT=../depend/ireader

SRCDIR = $(PRJ_DIR)/source/ \
		 $(PRJ_DIR)/source/rtsp/ \
		 $(PRJ_DIR)/source/rtsp/media/ \
		 $(DEPENDENT)/media-server/librtsp/test \
		 $(DEPENDENT)/media-server/librtsp/source/server/aio \
		 $(DEPENDENT)/sdk/source \
		 $(DEPENDENT)/sdk/source/digest \
		 $(DEPENDENT)/sdk/source/i18n \
		 $(DEPENDENT)/sdk/source/port \
		 ../common/src \

INCDIR =  		$(PRJ_DIR)/include   \
		$(DEPENDENT)/sdk/libaio/include \
		$(DEPENDENT)/media-server/libdash/include\
		$(DEPENDENT)/media-server/librtsp/include \
		$(DEPENDENT)/media-server/librtsp/test/ \
		$(DEPENDENT)/media-server/librtsp/test/media \
		$(DEPENDENT)/media-server/librtp/include \
		$(DEPENDENT)/sdk/libhttp/include \
		$(DEPENDENT)/sdk/libice/include \
		$(DEPENDENT)/sdk/include \
		$(DEPENDENT)/sdk/source/port \
		../reagent/include/video_proc \
		../rtsprec/include \
		../common/include \
		../common/include/video \
				



# Set search paths for source files
# Add any subdirs here
#------------------------------------
ADDITIONALSOURCE = 

MKINCLUDES =

INCLUDES = $(patsubst %,-I%,$(subst :, ,$(INCDIR)))


-include $(MKINCLUDES)

SOURCES =  \
nls_cp936.c \
sockpair.c \
unishox1.c \
uri-parse.c \
urlcodec.c \
rtsp-server-listen.c \
rtsp-server-tcp.c \
rtsp-server-udp.c \
rtpdump-file-reader.cpp \
rtpdump-file-source.cpp \
ra-rtsp-server.cpp \
rtp-udp-transport.cpp \
ip-route.c \
ntp-time.c \

STATIC_LIBS = $(DEPENDENT)/media-server/$(RA_BUILD_DIR)/lib/librtsp.a \
				$(DEPENDENT)/sdk/$(RA_BUILD_DIR)/lib/libhttp.a \


OBJS := $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCES)))		
OBJ_FILES = $(patsubst %,$(OBJDIR)/%,$(subst :, ,$(OBJS)))
		
INCLUDE_FILES = \

#Directory Search Paths
vpath %.c $(SRCDIR) $(ADDITIONALSOURCE)
vpath %.cpp $(SRCDIR) $(ADDITIONALSOURCE)
vpath %.h $(INCDIR)


LIBDIR = $(PRJ_DIR)/lib
LIBPATHS = $(DEPENDENT)/sdk/$(RA_BUILD_DIR)/lib/
LDFLAGS =

LIBS = aio
ifneq ($(findstring nto, $(CROSS_PFX)),nto)
   LIBS += rt pthread dl
else
   LIBS += socket
endif

# Generic Rules
${OBJDIR}/%.o: %.c
	$(CC) -c  $(CDEFINES) $(CFLAGS) $(INCLUDES) -o $@ $< 
${OBJDIR}/%.o: %.cpp
	$(CXX) -c  $(CDEFINES) $(CFLAGS) $(INCLUDES) -o $@ $< 

all:	$(PROG) $(INCLUDE_FILES)	


# bin directory 
# creates the bin directory if it doesn't exist
#------------------------------------
$(OBJDIR):
	mkdir -p $(OBJDIR) 

$(BINDIR):
	mkdir -p $(BINDIR)

-include $(OBJDIR)/*.d


$(PROG): $(OBJDIR) $(BINDIR) Makefile $(LDSCRIPT) $(OBJ_FILES) 
	@echo ======================  Link  ================================	
	$(CXX) -o ${BINDIR}/$(PROG) -Wl,-rpath=/opt/reagent/lib $(OBJ_FILES) $(LDFLAGS) $(addprefix -L,$(LIBPATHS)) $(addprefix -l,$(LIBS)) $(STATIC_LIBS)
ifeq ($(BUILD_SPEC),release)
	$(STRIP) ${BINDIR}/$(PROG)
endif

clean:
	rm -rf $(OBJDIR)
	rm -rf $(BINDIR)
	rm -rf $(RA_BUILD_DIR)






