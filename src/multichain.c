/*
 * Generic opt-in consensus/serialization capabilities for Bitcoin-derived
 * chains. This module deliberately contains no coin-name conditionals.
 */
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ckpool.h"
#include "libckpool.h"
#include "bitcoin.h"
#include "multichain.h"
#include "stratifier.h"
#include "yyjson.h"

struct gbt_output_rule {
	char *amount_path;
	char *address_path;
	char *script_path;
	bool optional;
};

struct multichain_caps {
	bool valid;
	bool coinbase_txntime;
	bool validate_coinbase;
	bool preciousblock;
	char *block_suffix;
	char *gbttarget;
	int gbtoutputs;
	struct gbt_output_rule outputs[MAX_GBT_COINBASE_OUTPUTS];
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

static bool valid_pointer(const char *ptr)
{
	return ptr && (!*ptr || *ptr == '/');
}

static bool copy_pointer(char **dst, yyjson_val *obj, const char *name, bool required)
{
	yyjson_val *value = yyjson_obj_get(obj, name);
	const char *ptr;

	*dst = NULL;
	if (!value || yyjson_is_null(value))
		return !required;
	if (!yyjson_is_str(value)) {
		LOGERR("%s must be a JSON pointer string", name);
		return false;
	}
	ptr = yyjson_get_str(value);
	if (!valid_pointer(ptr)) {
		LOGERR("%s must be an RFC6901-style JSON pointer", name);
		return false;
	}
	*dst = strdup(ptr);
	return true;
}

static void load_caps(void)
{
	yyjson_read_err err;
	yyjson_doc *doc = NULL;
	yyjson_val *root, *value;
	const char *suffix;
	size_t i, count;

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

	if (!copy_pointer(&caps.gbttarget, root, "gbttarget", false)) {
		caps.valid = false;
		goto out;
	}

	value = yyjson_obj_get(root, "gbtoutputs");
	if (value && !yyjson_is_null(value)) {
		if (!yyjson_is_arr(value)) {
			LOGERR("gbtoutputs must be an array");
			caps.valid = false;
			goto out;
		}
		count = yyjson_arr_size(value);
		if (count > MAX_GBT_COINBASE_OUTPUTS) {
			LOGERR("gbtoutputs has %zu entries; maximum is %d", count,
			       MAX_GBT_COINBASE_OUTPUTS);
			caps.valid = false;
			goto out;
		}
		for (i = 0; i < count; i++) {
			struct gbt_output_rule *rule = &caps.outputs[i];
			yyjson_val *entry = yyjson_arr_get(value, i);
			yyjson_val *opt;

			if (!yyjson_is_obj(entry) ||
			    !copy_pointer(&rule->amount_path, entry, "amount", true) ||
			    !copy_pointer(&rule->address_path, entry, "address", false) ||
			    !copy_pointer(&rule->script_path, entry, "script", false)) {
				LOGERR("Invalid gbtoutputs entry %zu", i);
				caps.valid = false;
				goto out;
			}
			if (!!rule->address_path == !!rule->script_path) {
				LOGERR("gbtoutputs entry %zu must contain exactly one of address or script", i);
				caps.valid = false;
				goto out;
			}
			opt = yyjson_obj_get(entry, "optional");
			if (opt && !yyjson_is_null(opt)) {
				if (!yyjson_is_bool(opt)) {
					LOGERR("gbtoutputs optional must be boolean");
					caps.valid = false;
					goto out;
				}
				rule->optional = yyjson_get_bool(opt);
			}
			caps.gbtoutputs++;
		}
	}

	if (caps.coinbase_txntime)
		LOGNOTICE("Optional coinbase transaction nTime enabled");
	if (caps.block_suffix)
		LOGNOTICE("Optional block suffix enabled (%zu bytes)", strlen(caps.block_suffix) / 2);
	if (!caps.validate_coinbase)
		LOGNOTICE("Coinbase decoderawtransaction validation disabled by configuration");
	if (!caps.preciousblock)
		LOGNOTICE("Post-submit preciousblock disabled by configuration");
	if (caps.gbtoutputs)
		LOGNOTICE("Configured %d GBT-defined mandatory coinbase output(s)", caps.gbtoutputs);
	if (caps.gbttarget)
		LOGNOTICE("Configured alternate effective target pointer %s", caps.gbttarget);
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

static void decode_pointer_segment(char *segment)
{
	char *src = segment, *dst = segment;

	while (*src) {
		if (src[0] == '~' && src[1] == '0') {
			*dst++ = '~';
			src += 2;
		} else if (src[0] == '~' && src[1] == '1') {
			*dst++ = '/';
			src += 2;
		} else {
			*dst++ = *src++;
		}
	}
	*dst = '\0';
}

static yyjson_val *pointer_get(yyjson_val *root, const char *pointer)
{
	yyjson_val *cur = root;
	char *copy, *walk, *segment;

	if (!pointer)
		return NULL;
	if (!*pointer)
		return root;
	if (*pointer != '/')
		return NULL;
	copy = strdup(pointer + 1);
	walk = copy;
	while ((segment = strsep(&walk, "/"))) {
		decode_pointer_segment(segment);
		if (yyjson_is_obj(cur)) {
			cur = yyjson_obj_get(cur, segment);
		} else if (yyjson_is_arr(cur)) {
			char *end = NULL;
			unsigned long idx;

			errno = 0;
			idx = strtoul(segment, &end, 10);
			if (errno || !*segment || (end && *end) || idx >= yyjson_arr_size(cur)) {
				cur = NULL;
				break;
			}
			cur = yyjson_arr_get(cur, idx);
		} else {
			cur = NULL;
		}
		if (!cur)
			break;
	}
	free(copy);
	return cur;
}

bool multichain_apply_gbt(struct genwork *gbt, yyjson_val *result)
{
	uint64_t mandatory_total = 0;
	int i;

	ensure_caps();
	if (!caps.valid)
		return false;
	gbt->mandatory_outputs = 0;
	gbt->effective_diff = 0;

	if (caps.gbttarget) {
		yyjson_val *value = pointer_get(result, caps.gbttarget);

		/* Match the eCash reference semantics: if the RTT field is absent,
		 * retain the ordinary GBT target rather than failing the template. */
		gbt->effective_diff = gbt->diff;
		if (value && !yyjson_is_null(value)) {
			const char *bits = yyjson_get_str(value);
			char compact[4];

			if (!bits || strlen(bits) != 8 || !validhex(bits) ||
			    !hex2bin(compact, bits, sizeof(compact))) {
				LOGERR("Invalid compact target at %s", caps.gbttarget);
				return false;
			}
			gbt->effective_diff = diff_from_nbits(compact);
			if (gbt->effective_diff <= 0) {
				LOGERR("Invalid effective difficulty from %s", caps.gbttarget);
				return false;
			}
		}
	}

	for (i = 0; i < caps.gbtoutputs; i++) {
		struct gbt_output_rule *rule = &caps.outputs[i];
		struct gbt_coinbase_output *out;
		yyjson_val *amount_val = pointer_get(result, rule->amount_path);
		int64_t signed_amount;
		uint64_t amount;

		if (!amount_val || yyjson_is_null(amount_val)) {
			if (rule->optional)
				continue;
			LOGERR("Missing mandatory GBT output amount at %s", rule->amount_path);
			return false;
		}
		if (!yyjson_is_int(amount_val)) {
			LOGERR("GBT output amount at %s must be an integer", rule->amount_path);
			return false;
		}
		signed_amount = yyjson_get_sint(amount_val);
		if (signed_amount < 0) {
			LOGERR("GBT output amount at %s is negative", rule->amount_path);
			return false;
		}
		amount = (uint64_t)signed_amount;
		if (!amount)
			continue;
		if (gbt->mandatory_outputs >= MAX_GBT_COINBASE_OUTPUTS)
			return false;
		out = &gbt->mandatory_output[gbt->mandatory_outputs];
		memset(out, 0, sizeof(*out));
		out->amount = amount;

		if (rule->address_path) {
			yyjson_val *address_val = pointer_get(result, rule->address_path);
			const char *address = yyjson_get_str(address_val);
			int script_len;

			if (!address) {
				if (rule->optional)
					continue;
				LOGERR("Missing GBT output address at %s", rule->address_path);
				return false;
			}
			script_len = payout_address_to_txn((char *)out->script, address, false, false);
			if (script_len <= 0 || script_len > MAX_GBT_OUTPUT_SCRIPT_LEN) {
				LOGERR("Unable to construct GBT output script for %s", address);
				return false;
			}
			out->script_len = script_len;
		} else {
			yyjson_val *script_val = pointer_get(result, rule->script_path);
			const char *script_hex = yyjson_get_str(script_val);
			size_t script_len;

			if (!script_hex) {
				if (rule->optional)
					continue;
				LOGERR("Missing GBT output script at %s", rule->script_path);
				return false;
			}
			if (!valid_hex_string(script_hex)) {
				LOGERR("Invalid GBT output script at %s", rule->script_path);
				return false;
			}
			script_len = strlen(script_hex) / 2;
			if (!script_len || script_len > MAX_GBT_OUTPUT_SCRIPT_LEN ||
			    !hex2bin(out->script, script_hex, script_len)) {
				LOGERR("GBT output script at %s is too large or invalid", rule->script_path);
				return false;
			}
			out->script_len = script_len;
		}
		mandatory_total += amount;
		if (mandatory_total > gbt->coinbasevalue) {
			LOGERR("GBT mandatory outputs exceed coinbasevalue");
			return false;
		}
		gbt->mandatory_outputs++;
	}
	return true;
}
