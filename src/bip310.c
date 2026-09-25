/*
 * BIP310 version-rolling helpers.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bip310.h"

static int hex_nibble(const char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

bool bip310_parse_mask(const char *mask, uint32_t *out)
{
	uint32_t value = 0;
	int i;

	if (!mask || !out || strlen(mask) != 8)
		return false;
	for (i = 0; i < 8; ++i) {
		int nibble = hex_nibble(mask[i]);

		if (nibble < 0)
			return false;
		value = (value << 4) | (uint32_t)nibble;
	}
	*out = value;
	return true;
}

void bip310_format_mask(char out[9], uint32_t mask)
{
	snprintf(out, 9, "%08x", mask);
}

unsigned int bip310_popcount(uint32_t mask)
{
	unsigned int n = 0;

	while (mask) {
		mask &= mask - 1;
		++n;
	}
	return n;
}
