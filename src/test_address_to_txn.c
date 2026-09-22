/*
 * Regression tests for address_to_txn().
 * Block 19243 on 5TRAT exposed that an explicit CashAddr was being fed to
 * b58tobin(), producing a valid P2PKH script for the wrong hash160.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include "libckpool.h"

static int expect_script(const char *address, const char *expected)
{
	char script[128] = {};
	char *hex;
	int len, ret = 0;

	len = address_to_txn(script, address, false, false);
	if (!len) {
		fprintf(stderr, "address_to_txn rejected %s\n", address);
		return 1;
	}
	hex = bin2hex(script, len);
	if (strcmp(hex, expected)) {
		fprintf(stderr, "%s\nexpected %s\nactual   %s\n", address, expected, hex);
		ret = 1;
	}
	free(hex);
	return ret;
}

int main(void)
{
	const char *p2pkh = "76a914a225d3a26b82e097119dd7a8621ba5fa7bfa02b888ac";
	int failed = 0;
	char script[128] = {};

	/* Same hash160 under three CashAddr prefixes used by our lanes/family. */
	failed += expect_script(
		"5trat:qz3zt5azdwpwp9c3nht6scsm5ha8h7szhqxtdmhlz5", p2pkh);
	failed += expect_script(
		"bitcoincash:qz3zt5azdwpwp9c3nht6scsm5ha8h7szhqqn8da35w", p2pkh);
	failed += expect_script(
		"ecash:qz3zt5azdwpwp9c3nht6scsm5ha8h7szhqe7nxxtje", p2pkh);

	/* Mixed case and checksum corruption must fail closed. */
	if (address_to_txn(script,
		"5trat:qz3zt5azdwpwp9c3nht6scsm5ha8h7szhqxtdmhlzQ5",
		false, false) != 0) {
		fprintf(stderr, "mixed-case CashAddr unexpectedly accepted\n");
		failed++;
	}
	if (address_to_txn(script,
		"5trat:qz3zt5azdwpwp9c3nht6scsm5ha8h7szhqxtdmhlzq",
		false, false) != 0) {
		fprintf(stderr, "bad-checksum CashAddr unexpectedly accepted\n");
		failed++;
	}

	return failed ? 1 : 0;
}
