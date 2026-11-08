CC ?= gcc
PKG_CONFIG ?= pkg-config
PROFILE ?= radio
CFLAGS ?= -g -O0 -std=gnu23 -Wall -Wextra
GST_CFLAGS := $(shell $(PKG_CONFIG) --cflags gstreamer-1.0)
GST_LIBS := $(shell $(PKG_CONFIG) --libs gstreamer-1.0)

.PHONY: check clean
check: test_pipeline test_json
	./test_pipeline
	./test_json

test_pipeline: test_pipeline.c ../pipeline.c ../pipeline.h
	$(CC) $(CFLAGS) $(GST_CFLAGS) -I.. -o $@ test_pipeline.c ../pipeline.c $(GST_LIBS)

AXE_TEST_CFLAGS := $(shell $(PKG_CONFIG) --cflags librustyaxe 2>/dev/null)
AXE_TEST_LIBS := $(shell $(PKG_CONFIG) --libs-only-L librustyaxe 2>/dev/null) -lrustyaxe

test_json: test_json.c ../../build/$(PROFILE)/build_config.h
	$(CC) $(CFLAGS) $(AXE_TEST_CFLAGS) -I../../build/$(PROFILE) -o $@ test_json.c $(AXE_TEST_LIBS)

clean:
	rm -f test_pipeline test_json
