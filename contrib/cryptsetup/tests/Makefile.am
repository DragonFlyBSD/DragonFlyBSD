TESTS = api-test compat-test align-test

EXTRA_DIST = fileDiffer.py compatimage.img.bz2 align-test compat-test

api_test_SOURCES = api-test.c
api_test_LDADD = ../lib/libcryptsetup.la
api_test_LDFLAGS = -static
api_test_CFLAGS = -g -Wall -O0 -I$(top_srcdir)/lib/

check_PROGRAMS = api-test

compatimage.img:
	@bzip2 -k -d compatimage.img.bz2
