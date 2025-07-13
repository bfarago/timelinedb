# ============================================================================
# Makefile for timelinedb project (root level)
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
SUBDIRS := src

all clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@ || exit 1; \
	done