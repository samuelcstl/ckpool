/*
 * Generic CashAddr decoder for Bitcoin-derived chains.
 *
 * This implementation intentionally handles only the address forms ckpool
 * needs to construct payout scripts: hash160 P2PKH and P2SH payloads. Network
 * policy is supplied by the caller as the expected CashAddr prefix.
 */

#include "config.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cashaddr.h"
#include "libckpool.h"

static const int8_t charset_rev[128] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	15, -1, 10, 17, 21, 20, 26, 30,  7,  5, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, 29, -1, 24, 13, 25,  9,  8, 23, -1, 18, 22, 31, 27, 19, -1,
	 1,  0,  3, 16, 11, 28, 12, 14,  6,  4,  2, -1, -1, -1, -1, -1
};

static uint64_t polymod(const uint8_t *values, size_t len)
{
	uint64_t c = 1;
	size_t i;

	for (i = 0; i < len; ++i) {
		uint8_t c0 = c >> 35;

		c = ((c & 0x07ffffffffULL) << 5) ^ values[i];
		if (c0 & 0x01) c ^= 0x98f2bc8e61ULL;
		if (c0 & 0x02) c ^= 0x79b76d99e2ULL;
		if (c0 & 0x04) c ^= 0xf33e5fb3c4ULL;
		if (c0 & 0x08) c ^= 0xae2eabe2a8ULL;
		if (c0 & 0x10) c ^= 0x1e4f43e470ULL;
	}
	return c;
}

static bool convert_bits_5to8(uint8_t *out, size_t *outlen,
                              const uint8_t *in, size_t inlen)
{
	uint32_t acc = 0;
	int bits = 0;
	size_t i;

	*outlen = 0;
	for (i = 0; i < inlen; ++i) {
		uint8_t value = in[i];

		if (value >= 32)
			return false;
		acc = (acc << 5) | value;
		bits += 5;
		while (bits >= 8) {
			bits -= 8;
			out[(*outlen)++] = (acc >> bits) & 0xff;
		}
	}

	return !(bits >= 5 ||
	         (bits > 0 && ((acc << (8 - bits)) & 0xff) != 0));
}

static bool case_is_consistent(const char *s)
{
	bool lower = false, upper = false;

	for (; *s; ++s) {
		if (*s >= 'a' && *s <= 'z')
			lower = true;
		else if (*s >= 'A' && *s <= 'Z')
			upper = true;
	}
	return !(lower && upper);
}

bool cashaddr_decode(const char *addr, const char *expected_prefix,
                     uint8_t hash160[20], bool *is_p2sh)
{
	const char *sep, *payload;
	uint8_t data[112], decoded[65], checksum_input[256];
	size_t payload_len, data_len = 0, prefix_len, idx = 0;
	size_t decoded_len, payload_data_len, i;
	uint8_t version, type;

	if (!addr || !*addr || !expected_prefix || !*expected_prefix ||
	    !hash160 || !is_p2sh)
		return false;
	if (!case_is_consistent(addr))
		return false;

	prefix_len = strlen(expected_prefix);
	if (prefix_len > 83)
		return false;

	sep = strchr(addr, ':');
	if (sep) {
		size_t supplied_len = (size_t)(sep - addr);

		if (supplied_len != prefix_len ||
		    strncasecmp(addr, expected_prefix, supplied_len))
			return false;
		payload = sep + 1;
	} else {
		payload = addr;
	}

	payload_len = strlen(payload);
	if (payload_len < 14 || payload_len > sizeof(data))
		return false;

	for (i = 0; i < payload_len; ++i) {
		unsigned char c = (unsigned char)tolower((unsigned char)payload[i]);
		int8_t value;

		if (c >= sizeof(charset_rev))
			return false;
		value = charset_rev[c];
		if (value < 0)
			return false;
		data[data_len++] = (uint8_t)value;
	}

	/* CashAddr checksum is exactly eight 5-bit symbols. */
	if (data_len < 9)
		return false;

	if (prefix_len + 1 + data_len > sizeof(checksum_input))
		return false;
	for (i = 0; i < prefix_len; ++i)
		checksum_input[idx++] = ((uint8_t)tolower((unsigned char)expected_prefix[i])) & 0x1f;
	checksum_input[idx++] = 0;
	for (i = 0; i < data_len; ++i)
		checksum_input[idx++] = data[i];
	if (polymod(checksum_input, idx) != 1)
		return false;

	payload_data_len = data_len - 8;
	if (payload_data_len <= 1)
		return false;
	if (!convert_bits_5to8(decoded, &decoded_len, data, payload_data_len))
		return false;

	/* ckpool currently needs only version + 20-byte hash160 forms. */
	if (decoded_len != 21)
		return false;
	version = decoded[0];
	if (version & 0x80)
		return false;
	type = (version >> 3) & 0x0f;
	if (type != 0 && type != 1)
		return false;
	/* Size code 0 is the 160-bit payload supported here. */
	if ((version & 0x07) != 0)
		return false;

	*is_p2sh = type == 1;
	memcpy(hash160, decoded + 1, 20);
	return true;
}

static int hash160_to_script(uint8_t *script, const uint8_t hash160[20], bool p2sh)
{
	if (p2sh) {
		script[0] = 0xa9; /* OP_HASH160 */
		script[1] = 0x14;
		memcpy(script + 2, hash160, 20);
		script[22] = 0x87; /* OP_EQUAL */
		return 23;
	}

	script[0] = 0x76; /* OP_DUP */
	script[1] = 0xa9; /* OP_HASH160 */
	script[2] = 0x14;
	memcpy(script + 3, hash160, 20);
	script[23] = 0x88; /* OP_EQUALVERIFY */
	script[24] = 0xac; /* OP_CHECKSIG */
	return 25;
}

int cashaddr_to_script(const char *addr, const char *expected_prefix,
                       uint8_t *script, bool *is_p2sh)
{
	uint8_t hash160[20];
	bool p2sh;

	if (!script || !cashaddr_decode(addr, expected_prefix, hash160, &p2sh))
		return 0;
	if (is_p2sh)
		*is_p2sh = p2sh;
	return hash160_to_script(script, hash160, p2sh);
}

bool cashaddr_normalize(char *dst, size_t dst_len, const char *addr,
                        const char *expected_prefix)
{
	uint8_t hash160[20];
	bool is_p2sh;
	const char *payload;
	size_t prefix_len, payload_len, i;

	if (!dst || !dst_len ||
	    !cashaddr_decode(addr, expected_prefix, hash160, &is_p2sh))
		return false;

	payload = strchr(addr, ':');
	payload = payload ? payload + 1 : addr;
	prefix_len = strlen(expected_prefix);
	payload_len = strlen(payload);
	if (prefix_len + 1 + payload_len + 1 > dst_len)
		return false;

	for (i = 0; i < prefix_len; ++i)
		dst[i] = (char)tolower((unsigned char)expected_prefix[i]);
	dst[prefix_len] = ':';
	for (i = 0; i < payload_len; ++i)
		dst[prefix_len + 1 + i] = (char)tolower((unsigned char)payload[i]);
	dst[prefix_len + 1 + payload_len] = '\0';
	return true;
}
