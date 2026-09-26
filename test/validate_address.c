/* Regression tests for node-authoritative payout address classification. */

#include "config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ckpool.h"
#include "bitcoin.h"
#include "yyjson.h"

ckpool_t ckpool;

static int classify(const char *json, const char *address,
		    bool want_ok, bool want_script, bool want_segwit)
{
	yyjson_doc *doc;
	yyjson_val *root;
	bool script = true, segwit = true, ok;

	doc = yyjson_read(json, strlen(json), 0);
	if (!doc)
		return 1;
	root = yyjson_doc_get_root(doc);
	ok = classify_validate_address(address, root, &script, &segwit);
	yyjson_doc_free(doc);

	if (ok != want_ok)
		return 2;
	if (ok && script != want_script)
		return 3;
	if (ok && segwit != want_segwit)
		return 4;
	return 0;
}

int main(void)
{
	int rc;

	/*
	 * Friotaioch P2QR witness-v2 response: node-authoritative witness metadata
	 * is present while isscript is deliberately omitted.
	 */
	rc = classify(
		"{\"isvalid\":true,\"address\":"
		"\"frio1zzlq6xyuhncmrvfm7j524aq8m7nzuclc973qxk45jltyh6x78j8jqg6s5fp\","
		"\"scriptPubKey\":"
		"\"522017c1a313979e3636277e95155e80fbf4c5cc7f05f4406b5692fac97d1bc791e4\","
		"\"iswitness\":true,\"witness_version\":2,"
		"\"witness_program\":"
		"\"17c1a313979e3636277e95155e80fbf4c5cc7f05f4406b5692fac97d1bc791e4\"}",
		"frio1zzlq6xyuhncmrvfm7j524aq8m7nzuclc973qxk45jltyh6x78j8jqg6s5fp",
		true, false, true);
	if (rc) {
		fprintf(stderr, "FAIL: witness-v2 without isscript rc=%d\n", rc);
		return 1;
	}

	/* Existing witness responses with isscript remain unchanged. */
	rc = classify(
		"{\"isvalid\":true,\"isscript\":true,\"iswitness\":true,"
		"\"witness_version\":0}",
		"bc1qexample",
		true, true, true);
	if (rc) {
		fprintf(stderr, "FAIL: witness with isscript rc=%d\n", rc);
		return 2;
	}

	/* Existing legacy P2SH classification remains unchanged. */
	rc = classify(
		"{\"isvalid\":true,\"isscript\":true,\"iswitness\":false}",
		"3Example",
		true, true, false);
	if (rc) {
		fprintf(stderr, "FAIL: legacy explicit isscript rc=%d\n", rc);
		return 3;
	}

	/* Preserve the old-daemon Base58 fallback when both fields are absent. */
	rc = classify(
		"{\"isvalid\":true}",
		"3Example",
		true, true, false);
	if (rc) {
		fprintf(stderr, "FAIL: legacy prefix fallback rc=%d\n", rc);
		return 4;
	}

	/* An arbitrary non-Bitcoin prefix is not accepted without witness metadata. */
	rc = classify(
		"{\"isvalid\":true}",
		"frio1notdeclaredwitness",
		false, false, false);
	if (rc) {
		fprintf(stderr, "FAIL: unsupported missing metadata rc=%d\n", rc);
		return 5;
	}

	/* Node rejection remains authoritative. */
	rc = classify(
		"{\"isvalid\":false}",
		"frio1invalid",
		false, false, false);
	if (rc) {
		fprintf(stderr, "FAIL: invalid address rc=%d\n", rc);
		return 6;
	}

	puts("Validateaddress classification tests passed");
	return 0;
}
