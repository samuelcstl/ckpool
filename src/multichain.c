/*
 * Generic opt-in consensus/serialization capabilities for Bitcoin-derived
 * chains. This module deliberately contains no coin-name conditionals.
 */
#include "config.h"

#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "ckpool.h"
#include "libckpool.h"
#include "multichain.h"
#include "yyjson.h"

struct multichain_caps {
	bool valid;
	bool coinbase_txntime;
	bool validate_coinbase;
	bool preciousblock;
	char *block_suffix;
};

static pthread_once_t caps_once = PTHREAD_ONCE_INIT;
static struct multichain_caps caps;

static bool read_bool(yyjson_val *root, const char *name, bool *dst)
{
	yyjson_val *value = yyjson_obj_get(root, name);

	if (!value || yyjson_is_null(value))
		return true;
	if (!yyjson_is_bool(value)) {
		LOGERR("%s must be a boolean", name);
		return false;
	}
	*dst = yyjson_get_bool(value);
	return true;
}

static bool valid_hex_string(const char *hex)
{
	size_t i, len;

	if (!hex)
		return false;
	len = strlen(hex);
	if (len & 1)
		return false;
	for (i = 0; i < len; i++) {
		if (!isxdigit((unsigned char)hex[i]))
			return false;
	}
	return true;
}

static void load_caps(void)
{
	yyjson_read_err err;
	yyjson_doc *doc = NULL;
	yyjson_val *root, *value;
	const char *suffix;

	memset(&caps, 0, sizeof(caps));
	caps.valid = true;
	caps.validate_coinbase = true;
	caps.preciousblock = true;

	if (!ckpool.config)
		return;
	doc = yyjson_read_file(ckpool.config, YYJSON_READ_STOP_WHEN_DONE, NULL, &err);
	if (!doc) {
		LOGERR("Unable to read multichain capabilities from %s: %s",
		       ckpool.config, err.msg ? err.msg : "JSON error");
		caps.valid = false;
		return;
	}
	root = yyjson_doc_get_root(doc);
	if (!yyjson_is_obj(root)) {
		LOGERR("Multichain configuration root must be an object");
		caps.valid = false;
		goto out;
	}

	if (!read_bool(root, "coinbase_txntime", &caps.coinbase_txntime) ||
	    !read_bool(root, "validate_coinbase", &caps.validate_coinbase) ||
	    !read_bool(root, "preciousblock", &caps.preciousblock)) {
		caps.valid = false;
		goto out;
	}

	value = yyjson_obj_get(root, "block_suffix");
	if (value && !yyjson_is_null(value)) {
		if (!yyjson_is_str(value)) {
			LOGERR("block_suffix must be an even-length hexadecimal string");
			caps.valid = false;
			goto out;
		}
		suffix = yyjson_get_str(value);
		if (!valid_hex_string(suffix)) {
			LOGERR("block_suffix must be an even-length hexadecimal string");
			caps.valid = false;
			goto out;
		}
		if (*suffix)
			caps.block_suffix = strdup(suffix);
	}

	if (caps.coinbase_txntime)
		LOGNOTICE("Optional coinbase transaction nTime enabled");
	if (caps.block_suffix)
		LOGNOTICE("Optional block suffix enabled (%zu bytes)", strlen(caps.block_suffix) / 2);
	if (!caps.validate_coinbase)
		LOGNOTICE("Coinbase decoderawtransaction validation disabled by configuration");
	if (!caps.preciousblock)
		LOGNOTICE("Post-submit preciousblock disabled by configuration");
out:
	yyjson_doc_free(doc);
}

static inline void ensure_caps(void)
{
	pthread_once(&caps_once, load_caps);
}

bool multichain_config_valid(void)
{
	ensure_caps();
	return caps.valid;
}

bool multichain_coinbase_txntime(void)
{
	ensure_caps();
	return caps.valid && caps.coinbase_txntime;
}

bool multichain_validate_coinbase(void)
{
	ensure_caps();
	return !caps.valid || caps.validate_coinbase;
}

bool multichain_preciousblock(void)
{
	ensure_caps();
	return !caps.valid || caps.preciousblock;
}

const char *multichain_block_suffix(void)
{
	ensure_caps();
	return caps.valid ? caps.block_suffix : NULL;
}
