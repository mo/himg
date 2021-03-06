###############################################################################
# HIMG, by Marcus Geelnard, 2015
#
# This is free and unencumbered software released into the public domain.
#
# See LICENSE for details.
###############################################################################

CPP = g++
CPPFLAGS = -c -W -Wall -g -std=c++11
AR = ar
ARFLAGS = rcs

LIB_OBJS = common.o \
           decoder.o \
           downsampled.o \
           encoder.o \
           hadamard.o \
           huffman.o \
           quantize.o

ALL_OBJS = $(LIB_OBJS) chimg.o dhimg.o

.PHONY: all clean

all: chimg dhimg

clean:
	rm -rf chimg dhimg libhimg.a $(ALL_OBJS)

chimg: chimg.o libhimg.a
	$(CPP) -o chimg chimg.o -L. -lhimg -lfreeimage

dhimg: dhimg.o libhimg.a
	$(CPP) -o dhimg dhimg.o -L. -lhimg -lfreeimage

libhimg.a: $(LIB_OBJS)
	$(AR) $(ARFLAGS) $@ $(LIB_OBJS)

chimg.o: chimg.cpp encoder.h
	$(CPP) $(CPPFLAGS) -o $@ $<

dhimg.o: dhimg.cpp decoder.h
	$(CPP) $(CPPFLAGS) -o $@ $<

common.o: common.cpp common.h
	$(CPP) $(CPPFLAGS) -o $@ $<

downsampled.o: downsampled.cpp downsampled.h
	$(CPP) $(CPPFLAGS) -o $@ $<

encoder.o: encoder.cpp common.h downsampled.h encoder.h hadamard.h huffman.h quantize.h
	$(CPP) $(CPPFLAGS) -o $@ $<

hadamard.o: hadamard.cpp hadamard.h
	$(CPP) $(CPPFLAGS) -o $@ $<

huffman.o: huffman.cpp huffman.h
	$(CPP) $(CPPFLAGS) -o $@ $<

quantize.o: quantize.cpp quantize.h
	$(CPP) $(CPPFLAGS) -o $@ $<

