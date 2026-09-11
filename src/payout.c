/*
 * Optional chain-aware payout address handling.
 *
 * cashaddr_prefix is deliberately a generic configuration capability. When
 * absent, payout script construction is exactly the upstream Base58/SegWit
 * path. When present, valid CashAddr P2PKH/P2SH addresses are decoded locally
 * using that prefix before falling back to the standard address formats.
 */

#include "config.h"

#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "ckpool.h"
#include "libckpool.h"
#define CKPOOL_PAYOUT_IMPLEMENTATION
#include "bitcoin.h"
#include "cashaddr.h"
#include "yyjson.h"

static pthread_once_t cashaddr_prefix_once = PTHREAD_ONCE_INIT;
static char cashaddr_prefix[84];
static bool cashaddr_prefix_configured;
static bool cashaddr_prefix_valid = true;

static void load_cashaddr_prefix(void)
{
	yyjson_doc *doc = NULL;
	yyjson_val *root, *value;
	yyjson_read_err err;
	const char *prefix;
	size_t len, i;

	if (!ckpool.config)
		return;

	doc = yyjson_read_file(ckpool.config, YYJSON_READ_STOP_WHEN_DONE, NULL, &err);
	if (!doc)
		return;
	root = yyjson_doc_get_root(doc);
	if (!yyjson_is_obj(root))
		goto out;

	value = yyjson_obj_get(root, "cashaddr_prefix");
	if (!value || yyjson_is_null(value))
		goto out;

	cashaddr_prefix_configured = true;
	if (!yyjson_is_str(value)) {
		LOGERR("cashaddr_prefix must be a non-empty alphanumeric string");
		cashaddr_prefix_valid = false;
		goto out;
	}
	prefix = yyjson_get_str(value);
	len = prefix ? strlen(prefix) : 0;
	if (!len || len >= sizeof(cashaddr_prefix)) {
		LOGERR("cashaddr_prefix must contain between 1 and 83 characters");
		cashaddr_prefix_valid = false;
		goto out;
	}
	for (i = 0; i < len; ++i) {
		unsigned char c = (unsigned char)prefix[i];

		if (!isalnum(c)) {
			LOGERR("cashaddr_prefix must be alphanumeric");
			cashaddr_prefix_valid = false;
			goto out;
		}
		cashaddr_prefix[i] = (char)tolower(c);
	}
	cashaddr_prefix[len] = '\0';
	LOGNOTICE("CashAddr payout prefix enabled: %s", cashaddr_prefix);
out:
	yyjson_doc_free(doc);
}

int payout_address_to_txn(char *p2h, const char *addr, const bool script,
                          const bool segwit)
{
	const char *prefix = NULL;

	pthread_once(&cashaddr_prefix_once, load_cashaddr_prefix);
	if (cashaddr_prefix_configured) {
		if (unlikely(!cashaddr_prefix_valid))
			return 0;
		prefix = cashaddr_prefix;
	}

	return cashaddr_or_standard_to_script((uint8_t *)p2h, addr, prefix,
	                                      script, segwit);
}
