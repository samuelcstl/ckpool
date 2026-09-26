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

/*
 * Classify a node-authoritative validateaddress result for payout construction.
 *
 * Some Core-family chains define additional witness destination types whose
 * validateaddress response has iswitness/witness_version but deliberately no
 * isscript field. Witness payout construction does not need the P2SH/P2WSH
 * distinction because address_to_txn() takes the witness path first, so a
 * missing isscript must not make an otherwise valid witness address fail.
 *
 * Keep the legacy prefix fallback only for non-witness responses from old
 * daemons that omit isscript entirely.
 */
bool classify_validate_address(const char *address, yyjson_val *res_val,
			       bool *script, bool *segwit)
{
	yyjson_val *valid_val, *script_val, *witness_val;

	if (unlikely(!address || !res_val || !script || !segwit))
		return false;

	*script = false;
	*segwit = false;

	valid_val = yyjson_obj_get(res_val, "isvalid");
	if (!valid_val || !yyjson_is_true(valid_val))
		return false;

	witness_val = yyjson_obj_get(res_val, "iswitness");
	script_val = yyjson_obj_get(res_val, "isscript");

	if (witness_val && yyjson_is_true(witness_val)) {
		*segwit = true;
		if (script_val)
			*script = yyjson_is_true(script_val);
		return true;
	}

	if (script_val) {
		*script = yyjson_is_true(script_val);
		return true;
	}

	/*
	 * Ancient daemons may omit both fields. Preserve the historical Base58
	 * heuristic here, but do not apply it to a node-declared witness address.
	 */
	if (address[0] == '3' || address[0] == '2')
		*script = true;
	else if (address[0] != '1' && address[0] != 'm')
		return false;

	return true;
}

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

bool payout_local_codec_enabled(void)
{
	pthread_once(&cashaddr_prefix_once, load_cashaddr_prefix);
	return cashaddr_prefix_configured && cashaddr_prefix_valid;
}

bool payout_address_is_cashaddr(const char *addr, bool *script, bool *segwit)
{
	uint8_t hash160[20];
	bool is_p2sh;

	pthread_once(&cashaddr_prefix_once, load_cashaddr_prefix);
	if (!cashaddr_prefix_configured || unlikely(!cashaddr_prefix_valid))
		return false;
	if (!cashaddr_decode(addr, cashaddr_prefix, hash160, &is_p2sh))
		return false;
	if (script)
		*script = is_p2sh;
	if (segwit)
		*segwit = false;
	return true;
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
