/*
 * Copyright (c) 2020,2021 by Solar Designer
 * See LICENSE
 */

#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE 1
#define _LARGEFILE64_SOURCE 1
#define _LARGE_FILES 1

#ifdef _MSC_VER
#define _CRT_NONSTDC_NO_WARNINGS /* we use POSIX function names */
#define _CRT_SECURE_NO_WARNINGS /* we use open() */
#include <stdio.h> /* for SEEK_SET and SEEK_END */
#include <io.h>
#define lseek _lseeki64
#define ssize_t int /* MSVC's read() returns int and we don't need more here */
#define SSIZE_MAX INT_MAX
#define OPEN_FLAGS (O_RDONLY | _O_BINARY | _O_RANDOM)
#else
#include <unistd.h>
#define OPEN_FLAGS O_RDONLY
#endif

#include <stdint.h>
#include <limits.h>
#include <fcntl.h>

#include "passwdqc.h"
#define PASSWDQC_FILTER_INTERNALS
#include "passwdqc_filter.h"

static ssize_t read_loop(int fd, void *buffer, size_t count)
{
	ssize_t offset, block;

	offset = 0;
	while (count > 0 && count <= SSIZE_MAX) {
		block = read(fd, (char *)buffer + offset, count);

		if (block < 0)
			return block;
		if (!block)
			return offset;

		offset += block;
		count -= block;
	}

	return offset;
}

int passwdqc_filter_open(passwdqc_filter_t *flt, const char *filename)
{
	if ((flt->fd = open(filename, OPEN_FLAGS)) < 0)
		return -1;

	if (read_loop(flt->fd, &flt->header, sizeof(flt->header)) != sizeof(flt->header) ||
	    passwdqc_filter_verify_header(&flt->header) ||
	    flt->header.hash_id < PASSWDQC_FILTER_HASH_MIN || flt->header.hash_id > PASSWDQC_FILTER_HASH_MAX ||
	    (size_t)lseek(flt->fd, 0, SEEK_END) != sizeof(flt->header) + (flt->header.capacity << 2)) {
		passwdqc_filter_close(flt);
		return -1;
	}

	return 0;
}

int passwdqc_filter_close(passwdqc_filter_t *flt)
{
	int retval = close(flt->fd);
	flt->fd = -1;
	return retval;
}

static int check(const passwdqc_filter_t *flt, passwdqc_filter_i_t i, passwdqc_filter_f_t f)
{
	int retval = -1;

	passwdqc_filter_packed_t p;
	if (lseek(flt->fd, sizeof(flt->header) + (uint64_t)i * sizeof(p), SEEK_SET) < 0 ||
	    read_loop(flt->fd, &p, sizeof(p)) != sizeof(p))
		goto out;

	passwdqc_filter_unpacked_t u;
	unsigned int n = (unsigned int)passwdqc_filter_unpack(&u, &p, NULL);
	if (n > flt->header.bucket_size)
		goto out;

	unsigned int j;
	for (j = 0; j < n; j++) {
		if (passwdqc_filter_f_eq(u.slots[j], f, flt->header.bucket_size)) {
			retval = 1;
			goto out;
		}
	}

	retval = (n < flt->header.threshold) ? 0 : 2;

out:
	_passwdqc_memzero(&p, sizeof(p));
	_passwdqc_memzero(&u, sizeof(u));
	return retval;
}

int passwdqc_filter_lookup(const passwdqc_filter_t *flt, const char *plaintext)
{
	int retval = 3;
	passwdqc_filter_hash_t h;
	passwdqc_filter_f_t ftest;

clean:
	switch (flt->header.hash_id) {
	case PASSWDQC_FILTER_HASH_MD4:
		passwdqc_filter_md4(&h, plaintext);
		ftest = 0x8c6420f439de2000ULL;
		break;
	case PASSWDQC_FILTER_HASH_NTLM_CP1252:
		passwdqc_filter_ntlm_cp1252(&h, plaintext);
		ftest = 0x26bd9256ff7e052eULL;
		break;
	default:
		return -1;
	}

	uint32_t nbuckets = (uint32_t)(flt->header.capacity >> 2);
	passwdqc_filter_i_t i = passwdqc_filter_h2i(&h, nbuckets);
	passwdqc_filter_f_t f = passwdqc_filter_h2f(&h);

	_passwdqc_memzero(&h, sizeof(h));

/*
 * The tests of i and f here not only self-test the code, but also prevent the
 * compiler from moving "return retval;" to before the computation of h, i, and
 * f, which would leave sensitive data from the real hash computation around.
 */
	if (i >= nbuckets)
		return -1;

	if (retval <= 1) {
/* Waste two syscalls on overwriting lseek()'s stack and current file offset */
		i = passwdqc_filter_h2i(&h, nbuckets); /* 0 */
		if (check(flt, i, f) < 0)
			return -1;
		if (f != ftest)
			return -1;
		return retval;
	}

/* At least 1 character to overwrite passwdqc_filter_ntlm_cp1252()'s buffer */
	plaintext = " 09AZaz\x7e\x7f\x80\x81\x9e\x9f\xa0\xff";

	retval = check(flt, i, f);
	if (retval <= 1)
		goto clean;

	retval = check(flt, passwdqc_filter_alti(i, f, nbuckets), f);
	if (retval == 2)
		retval = 0;
	goto clean;
}
