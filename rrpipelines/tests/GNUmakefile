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

test_json: test_json.c ../../librustyaxe.so ../../build/$(PROFILE)/build_config.h
	$(CC) $(CFLAGS) -I../.. -I../../build/$(PROFILE) -L../.. -Wl,-rpath,'$$ORIGIN/../..' -o $@ test_json.c -lrustyaxe

clean:
	rm -f test_pipeline test_json
