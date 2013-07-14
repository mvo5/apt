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

PRIVATES=list install output cachefile update cmndline moo
SOURCE += $(foreach private, $(PRIVATES), private-$(private).cc)
HEADERS += $(foreach private, $(PRIVATES), private-$(private).h)

SOURCE+= acqprogress.cc
HEADERS+= acqprogress.h private-cacheset.h

HEADERS := $(addprefix apt-private/,$(HEADERS))
include $(LIBRARY_H)
