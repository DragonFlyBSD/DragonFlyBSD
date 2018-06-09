NOMAN=
NOSHARED=
WARNS?=		6

# Define Makefile variable RESCUE
CRUNCH_BUILDOPTS+= -DRESCUE
# Define compile-time RESCUE symbol when compiling components
CRUNCH_BUILDOPTS+= CRUNCH_CFLAGS=-DRESCUE

# Use soft links to create the crunched program links
#CRUNCH_USE_SYMLINKS=
