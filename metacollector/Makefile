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
   LIBDIR := $(RA_BUILD_DIR)/lib
   BINDIR := $(RA_BUILD_DIR)/bin
   CFLAGS = -O3 -g0 -fPIC
else   
   OBJDIR := $(RA_BUILD_DIR)/objdebug
   LIBDIR := $(RA_BUILD_DIR)/libdebug
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


PRJ_DIR = .
PROG	= metacollector

SRCDIR = src ../common/src
INCDIR := include \
		  ../common/include \
		  ../common/include/video \
		  ../metarec/include \
		  ../reagent/include/video_proc 

# Set search paths for source files
# Add any subdirs here
#------------------------------------
ADDITIONALSOURCE = 

MKINCLUDES =

BUILD_SPEC     ?= release

INCLUDES = $(patsubst %,-I%,$(subst :, ,$(INCDIR)))

ifneq ($(findstring nto, $(CROSS_PFX)),nto)
   CFLAGS += -Wno-unused-result
endif 

-include $(MKINCLUDES)

SOURCES =  \
	main.c \
	metareader.c \
	dir_walker.c \
	metadata_util.c \
	xml_writer.c \
	metawriter.c \
	coc.c \
	metacoc.c

OBJS= $(SOURCES:.c=.o) \
	  $(CXXSOURCES:.cpp=.o)

ifeq ($(BITNESS), default)
   MFLAG=
   LIBDIR = ../lib
   LDFLAGS = -L$(LIBDIR) -lrt -lpthread -lptcapi -lptcsys -lptcutils -lm
else
   ifeq ($(BITNESS), 32)
      MFLAG = -m32
      LIBDIR = ../lib
      LDFLAGS = -L$(LIBDIR) -lrt -lpthread -lptcapi -lptcsys -lptcutils -lm 
   else
      MFLAG = -m64
      LIBDIR = ../lib64
      LDFLAGS = -L$(LIBDIR) -lrt -lpthread -lptcapi -lptcsys -lptcutils -lm 
   endif
endif

MFLAG=
LIBDIR = ../$(RA_BUILD_DIR)/lib
COMLIBDIR = ../common/$(RA_BUILD_DIR)/lib
LDFLAGS = -L$(LIBDIR) -L$(COMLIBDIR) -lptcapi -lptcsys -lptcutils

CFLAGS += $(MFLAG)
LDFLAGS += $(MFLAG)
LDFLAGS += -lm -lz -lssl -lcrypto

ifeq ($(findstring nto, $(CROSS_PFX)),nto)
   LDFLAGS += -lsocket
else   
   LDFLAGS += -lpthread -ldl -lrt
endif
		
OBJ_FILES = $(patsubst %,$(OBJDIR)/%,$(subst :, ,$(OBJS)))
	
INCLUDE_FILES = \

#Directory Search Paths
vpath %.c $(SRCDIR) $(ADDITIONALSOURCE)
vpath %.h $(INCDIR)

# Generic Rules
${OBJDIR}/%.o: %.c
	$(CC) -c $(CDEFINES) $(CFLAGS) $(INCLUDES) -o $@ $<

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
	@echo ======================  Link $(PROG) ================================	
	$(CC) -o ${BINDIR}/$(PROG) $(OBJ_FILES) $(LDFLAGS)
ifeq ($(BUILD_SPEC),release)
	$(STRIP) ${BINDIR}/$(PROG)
endif	

clean:
	rm -rf $(OBJDIR)/*.o $(OBJDIR)/*.d 
	rm -rf $(BINDIR)/$(PROG) 
	rm -f ./$(PROG) 
	rm -rf $(BINDIR)/$(PROG).exe 
	rm -rf $(RA_BUILD_DIR)

