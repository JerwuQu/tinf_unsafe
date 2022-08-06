#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "../tinf.h"

#include "test_raw.h"
#include "test_compressed.h"

#define COOKIE 0xff
#define TEST_RAW_SZ sizeof(test_raw)

unsigned char uncompress_buf[TEST_RAW_SZ + 1];

int main()
{
	uncompress_buf[TEST_RAW_SZ] = COOKIE;
	const unsigned int sz = tinf_uncompress(uncompress_buf, test_compressed);
	assert(sz == TEST_RAW_SZ);
	for (size_t i = 0; i < TEST_RAW_SZ; i++) {
		assert(uncompress_buf[i] == test_raw[i]);
	}
	assert(uncompress_buf[TEST_RAW_SZ] == COOKIE);
	return 0;
}
