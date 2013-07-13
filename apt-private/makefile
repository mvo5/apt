# -*- make -*-
BASE=..
SUBDIR=apt-private

# Header location
HEADER_TARGETDIRS = apt-private

# Bring in the default rules
include ../buildlib/defaults.mak

# The library name and version (indirectly used from init.h)
include ../buildlib/libversion.mak

# The library name
LIBRARY=apt-private
MAJOR=1.0
MINOR=0
SLIBS=$(PTHREADLIB) -lapt-pkg

# Source code for the main library
SOURCE+= private-list.cc private-install.cc private-output.cc \
	 private-cachefile.cc acqprogress.cc private-update.cc\
	 private-upgrade.cc

# header files
HEADERS = private-list.h private-install.h private-output.h \
	private-cacheset.h private-cachefile.h acqprogress.h \
	private-update.h private-upgrade.h

HEADERS := $(addprefix apt-private/,$(HEADERS))
include $(LIBRARY_H)
