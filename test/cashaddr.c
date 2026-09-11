/* Generic CashAddr and payout-script regression tests. */

#include "config.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cashaddr.h"
#include "libckpool.h"

static void expect_script(const char *addr, const char *prefix,
                          const char *expected_hex, bool expected_p2sh)
{
	uint8_t script[64], expected[64];
	bool p2sh = !expected_p2sh;
	int len;
	size_t expected_len = strlen(expected_hex) / 2;

	assert(expected_len <= sizeof(expected));
	assert(hex2bin(expected, expected_hex, expected_len));
	memset(script, 0, sizeof(script));
	len = cashaddr_to_script(addr, prefix, script, &p2sh);
	assert(len == (int)expected_len);
	assert(p2sh == expected_p2sh);
	assert(!memcmp(script, expected, expected_len));
}

static void expect_standard_unchanged(const char *addr, const char *prefix,
                                      bool script, bool segwit)
{
	uint8_t raw[128], adapted[128];
	int raw_len, adapted_len;

	memset(raw, 0, sizeof(raw));
	memset(adapted, 0, sizeof(adapted));
	raw_len = address_to_txn((char *)raw, addr, script, segwit);
	assert(raw_len > 0);
	adapted_len = cashaddr_or_standard_to_script(adapted, addr, prefix,
	                                             script, segwit);
	assert(adapted_len == raw_len);
	assert(!memcmp(adapted, raw, raw_len));
}

int main(void)
{
	static const char hash160[] = "76a04053bda0a88bda5177b86a15c3b29f559873";
	char p2pkh_script[51], p2sh_script[47];
	uint8_t decoded[20];
	bool p2sh;

	snprintf(p2pkh_script, sizeof(p2pkh_script), "76a914%s88ac", hash160);
	snprintf(p2sh_script, sizeof(p2sh_script), "a914%s87", hash160);

	/* Canonical BCH CashAddr vectors, including prefixless acceptance. */
	expect_script("bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a",
	              "bitcoincash", p2pkh_script, false);
	expect_script("qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a",
	              "bitcoincash", p2pkh_script, false);
	expect_script("bitcoincash:ppm2qsznhks23z7629mms6s4cwef74vcwvn0h829pq",
	              "bitcoincash", p2sh_script, true);

	/* Same payload under another configured network prefix. */
	expect_script("ecash:qpm2qsznhks23z7629mms6s4cwef74vcwva87rkuu2",
	              "ecash", p2pkh_script, false);
	expect_script("ecash:ppm2qsznhks23z7629mms6s4cwef74vcwv2zrv3l8h",
	              "ecash", p2sh_script, true);

	/* Prefix and case are consensus-relevant parts of CashAddr decoding. */
	assert(!cashaddr_decode(
		"bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a",
		"ecash", decoded, &p2sh));
	assert(!cashaddr_decode(
		"bitcoincash:Qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a",
		"bitcoincash", decoded, &p2sh));

	/* Enabling CashAddr must not perturb standard payout formats. */
	expect_standard_unchanged("1BpEi6DfDAUFd7GtittLSdBeYJvcoaVggu",
	                          "bitcoincash", false, false);
	expect_standard_unchanged("1BpEi6DfDAUFd7GtittLSdBeYJvcoaVggu",
	                          NULL, false, false);
	expect_standard_unchanged("bc1q28kkr5hk4gnqe3evma6runjrd2pvqyp8fpwfzu",
	                          "bitcoincash", false, true);
	expect_standard_unchanged("bc1q28kkr5hk4gnqe3evma6runjrd2pvqyp8fpwfzu",
	                          NULL, false, true);

	puts("CashAddr tests passed");
	return 0;
}
