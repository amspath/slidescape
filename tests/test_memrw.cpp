#include "common.h"
#include "doctest.h"

#include "memrw.h"

TEST_CASE("memrw grows and preserves written bytes") {
	memrw_t buffer = memrw_create(4);
	const char payload[] = "abcdef";

	CHECK(memrw_write(payload, &buffer, COUNT(payload)) == COUNT(payload));
	CHECK(buffer.used_size == COUNT(payload));
	CHECK(buffer.capacity >= COUNT(payload));

	memrw_seek(&buffer, 0);
	char readback[COUNT(payload)] = {};
	CHECK(memrw_read(readback, &buffer, COUNT(readback)) == COUNT(readback));
	CHECK(memcmp(readback, payload, COUNT(payload)) == 0);

	memrw_destroy(&buffer);
}

TEST_CASE("memrw_string_pool_push stores zero-terminated strings") {
	memrw_t buffer = memrw_create(8);
	i64 first = memrw_string_pool_push(&buffer, "alpha");
	i64 second = memrw_string_pool_push(&buffer, "beta");

	CHECK(strcmp((char*)buffer.data + first, "alpha") == 0);
	CHECK(strcmp((char*)buffer.data + second, "beta") == 0);
	CHECK(second == 6);

	memrw_destroy(&buffer);
}

TEST_CASE("memrw_write_string_urlencode escapes non-alphanumeric bytes") {
	memrw_t buffer = memrw_create(8);
	memrw_write_string_urlencode("A b+1", &buffer);
	memrw_putc('\0', &buffer);

	CHECK(strcmp((char*)buffer.data, "A%20b%2b1") == 0);

	memrw_destroy(&buffer);
}
