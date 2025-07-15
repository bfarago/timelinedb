# ============================================================================
# Makefile for timelinedb project (src level)
#
# Builds:
#   - devtest:    Console application for testing signal processing
#   - devgui:     SDL-based GUI for signal visualization
#   - libtimelinedb.a: Static library containing core signal processing logic
#
# Platform-specific optimization flags are applied automatically for:
#   - macOS (Apple M1)
#   - Linux ARM (aarch64)
#   - Generic fallback with native optimization
#
# Author: Barna Faragó 2025 - MYND-ideal kft.
# ============================================================================

CC = clang
CFLAGS = -Wall -Wextra -std=c99 -O3 -g
LDFLAGS = 
# APPLE or ARM specific flags
ifeq ($(shell uname -s),Darwin)
	CC = clang
	CFLAGS += -DAPPLE
	CFLAGSSIMD = -DAPPLE -O3 -ftree-vectorize -march=armv8-a+simd -mcpu=apple-m1 -fno-signed-zeros -ffast-math
else ifeq ($(shell uname -m),aarch64)
	CFLAGS += -DARM
	CFLAGSSIMD = -DARM -O3 -ftree-vectorize -march=armv8-a+simd -mcpu=cortex-a72 -fno-signed-zeros -ffast-math
else ifeq ($(shell uname -p),x86_64)
	CC = gcc
	CFLAGS = -Wall -Wextra -std=gnu11 -O3 -g -mavx2 -pg
	CFLAGSSIMD = -O3 -ftree-vectorize -march=native -fno-signed-zeros -ffast-math -std=c11 -mavx2
	LDFLAGS = -lm
endif

SOURCES_DEVTEST = devtest.c
SOURCES_DEVGUI  = devgui.c

TARGETS = devtest devgui

all: $(TARGETS)

libtimelinedb.a: timelinedb.o timelinedb_util.o timelinedb_simd.o
	ar rcs libtimelinedb.a timelinedb.o timelinedb_util.o timelinedb_simd.o

timelinedb.o: timelinedb.c
	$(CC) $(CFLAGS) -c timelinedb.c

timelinedb_simd.o: timelinedb_simd.c
	$(CC) $(CFLAGSSIMD) -c timelinedb_simd.c

timelinedb_util.o: timelinedb_util.c
	$(CC) $(CFLAGS) -c timelinedb_util.c

devtest: libtimelinedb.a $(SOURCES_DEVTEST)
	$(CC) $(CFLAGS) -o devtest $(SOURCES_DEVTEST) libtimelinedb.a $(LDFLAGS)

SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LDFLAGS := $(shell sdl2-config --libs) -lSDL2_ttf

devgui: libtimelinedb.a $(SOURCES_DEVGUI)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o devgui $(SOURCES_DEVGUI) libtimelinedb.a $(SDL_LDFLAGS) $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o libtimelinedb.a