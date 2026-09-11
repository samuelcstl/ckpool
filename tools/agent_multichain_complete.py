#!/usr/bin/env python3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:80]!r}")
    write(path, text.replace(old, new, 1))


def replace_count(path, old, new, count):
    text = read(path)
    found = text.count(old)
    if found != count:
        raise SystemExit(f"{path}: expected {count} matches, found {found}: {old[:80]!r}")
    write(path, text.replace(old, new))


MULTICHAIN_H_V1 = r'''/*
 * Generic opt-in consensus/serialization capabilities for Bitcoin-derived
 * chains. Defaults preserve upstream Bitcoin behavior.
 */
#ifndef MULTICHAIN_H
#define MULTICHAIN_H

#include <stdbool.h>

bool multichain_config_valid(void);
bool multichain_coinbase_txntime(void);
bool multichain_validate_coinbase(void);
bool multichain_preciousblock(void);
const char *multichain_block_suffix(void);

#endif /* MULTICHAIN_H */
'''

MULTICHAIN_C_V1 = r'''/*
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
'''

CHAINCAPS_TEST_V1 = r'''/* Generic multichain capability configuration regression tests. */
#include "config.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ckpool.h"
#include "multichain.h"

ckpool_t ckpool;

static void fail(const char *msg)
{
	fprintf(stderr, "FAIL: %s\n", msg);
	exit(1);
}

static char *write_config(const char *json)
{
	char tmpl[] = "/var/tmp/ckpool-chaincaps-XXXXXX";
	int fd = mkstemp(tmpl);
	FILE *f;

	if (fd < 0)
		fail("mkstemp");
	f = fdopen(fd, "w");
	if (!f)
		fail("fdopen");
	if (fputs(json, f) < 0 || fclose(f))
		fail("write config");
	return strdup(tmpl);
}

static int run_default(void)
{
	memset(&ckpool, 0, sizeof(ckpool));
	ckpool.config = NULL;
	if (!multichain_config_valid())
		return 1;
	if (multichain_coinbase_txntime())
		return 2;
	if (!multichain_validate_coinbase())
		return 3;
	if (!multichain_preciousblock())
		return 4;
	if (multichain_block_suffix())
		return 5;
	return 0;
}

static int run_configured(void)
{
	char *path = write_config(
		"{\"coinbase_txntime\":true,\"validate_coinbase\":false,"
		"\"preciousblock\":false,\"block_suffix\":\"00\"}\n");
	const char *suffix;
	int rc = 0;

	memset(&ckpool, 0, sizeof(ckpool));
	ckpool.config = path;
	if (!multichain_config_valid())
		rc = 1;
	else if (!multichain_coinbase_txntime())
		rc = 2;
	else if (multichain_validate_coinbase())
		rc = 3;
	else if (multichain_preciousblock())
		rc = 4;
	else if (!(suffix = multichain_block_suffix()) || strcmp(suffix, "00"))
		rc = 5;
	unlink(path);
	free(path);
	return rc;
}

static void run_child(int (*fn)(void), const char *name)
{
	pid_t pid = fork();
	int status;

	if (pid < 0)
		fail("fork");
	if (!pid)
		exit(fn());
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status)) {
		fprintf(stderr, "FAIL: %s rc=%d\n", name,
		        WIFEXITED(status) ? WEXITSTATUS(status) : 255);
		exit(1);
	}
}

int main(void)
{
	run_child(run_default, "defaults");
	run_child(run_configured, "configured");
	puts("Multichain capability tests passed");
	return 0;
}
'''


def phase1():
    write("src/multichain.h", MULTICHAIN_H_V1)
    write("src/multichain.c", MULTICHAIN_C_V1)
    write("test/chaincaps_test.c", CHAINCAPS_TEST_V1)

    replace_once(
        "src/Makefile.am",
        "ckpool_SOURCES = ckpool.c generator.c bitcoin.c payout.c stratifier.c connector.c \\\n\t\t yyjson.c yyjson_util.c",
        "ckpool_SOURCES = ckpool.c generator.c bitcoin.c payout.c multichain.c stratifier.c connector.c \\\n\t\t yyjson.c yyjson_util.c",
    )
    replace_once(
        "test/Makefile.am",
        "check_PROGRAMS = sha256 bip310 cashaddr\nTESTS = sha256 bip310 cashaddr\n\nsha256_SOURCES = sha256.c\nbip310_SOURCES = bip310.c\ncashaddr_SOURCES = cashaddr.c",
        "check_PROGRAMS = sha256 bip310 cashaddr chaincaps\nTESTS = sha256 bip310 cashaddr chaincaps\n\nsha256_SOURCES = sha256.c\nbip310_SOURCES = bip310.c\ncashaddr_SOURCES = cashaddr.c\nchaincaps_SOURCES = chaincaps_test.c $(top_srcdir)/src/multichain.c\nchaincaps_LDADD = $(top_srcdir)/src/libckpool.a",
    )
    replace_once(
        "src/bitcoin.c",
        '#include "bitcoin.h"\n#include "stratifier.h"',
        '#include "bitcoin.h"\n#include "multichain.h"\n#include "stratifier.h"',
    )
    replace_once(
        "src/bitcoin.c",
        '\tres_val = yyjson_obj_get(root, "result");\n\tif (!res_val) {\n\t\tLOGWARNING("Failed to get result in json response to getblocktemplate");\n\t\tgoto out;\n\t}\n',
        '\tres_val = yyjson_obj_get(root, "result");\n\tif (!res_val) {\n\t\tLOGWARNING("Failed to get result in json response to getblocktemplate");\n\t\tgoto out;\n\t}\n\tif (unlikely(!multichain_config_valid())) {\n\t\tLOGERR("Invalid multichain capability configuration");\n\t\tgoto out;\n\t}\n',
    )


def phase2():
    replace_once(
        "src/stratifier.c",
        '#include "bitcoin.h"\n#include "sha2.h"',
        '#include "bitcoin.h"\n#include "multichain.h"\n#include "sha2.h"',
    )
    replace_once(
        "src/stratifier.c",
        '\tchar header[272];\n\tint len, ofs = 0;\n\tts_t now;\n\n\t/* Set fixed length coinb1 arrays to be more than enough */\n\twb->coinb1 = ckzalloc(256);\n\twb->coinb1bin = ckzalloc(128);\n\n\t/* Strings in wb should have been zero memset prior. Generate binary\n\t * templates first, then convert to hex */\n\tmemcpy(wb->coinb1bin, scriptsig_header_bin, 41);\n\tofs += 41; // Fixed header length;\n\n\tofs++; // Script length is filled in at the end @wb->coinb1bin[41];',
        '\tchar header[272];\n\tint header_len, script_len_pos;\n\tint len, ofs = 0;\n\tts_t now;\n\n\t/* Set fixed length coinb1 arrays to be more than enough */\n\twb->coinb1 = ckzalloc(256);\n\twb->coinb1bin = ckzalloc(128);\n\n\t/* Strings in wb should have been zero memset prior. Generate binary\n\t * templates first, then convert to hex. Some Bitcoin-derived transaction\n\t * formats carry a transaction nTime directly after nVersion. */\n\tif (multichain_coinbase_txntime()) {\n\t\tu32 = htole32(wb->ntime32);\n\t\tmemcpy(wb->coinb1bin, scriptsig_header_bin, 4);\n\t\tmemcpy(wb->coinb1bin + 4, &u32, sizeof(u32));\n\t\tmemcpy(wb->coinb1bin + 8, scriptsig_header_bin + 4, 37);\n\t\theader_len = 45;\n\t} else {\n\t\tmemcpy(wb->coinb1bin, scriptsig_header_bin, 41);\n\t\theader_len = 41;\n\t}\n\tofs = header_len;\n\n\tscript_len_pos = ofs++; // Script length is filled in at the end.',
    )
    replace_once(
        "src/stratifier.c",
        '\tlen = wb->coinb1len - 41;\n',
        '\tlen = wb->coinb1len - header_len;\n',
    )
    replace_once(
        "src/stratifier.c",
        '\twb->coinb1bin[41] = len - 1; /* Set the length now */\n',
        '\twb->coinb1bin[script_len_pos] = len - 1; /* Set the length now */\n',
    )
    old_validation = '''\t\t\tcbstr = generator_checktxn(cb);\n\t\t\tif (cbstr) {\n\t\t\t\tLOGNOTICE("Coinbase transaction confirmed valid");\n\t\t\t\tLOGDEBUG("%s", cbstr);\n\t\t\t\tfree(cbstr);\n\t\t\t} else {\n\t\t\t\t/* This is a fatal error */\n\t\t\t\tLOGEMERG("Coinbase failed valid transaction check, aborting!");\n\t\t\t\texit(1);\n\t\t\t}\n'''
    new_validation = '''\t\t\tif (!multichain_validate_coinbase()) {\n\t\t\t\tLOGNOTICE("Coinbase RPC transaction validation disabled by configuration");\n\t\t\t} else {\n\t\t\t\tcbstr = generator_checktxn(cb);\n\t\t\t\tif (cbstr) {\n\t\t\t\t\tLOGNOTICE("Coinbase transaction confirmed valid");\n\t\t\t\t\tLOGDEBUG("%s", cbstr);\n\t\t\t\t\tfree(cbstr);\n\t\t\t\t} else {\n\t\t\t\t\t/* This is a fatal error */\n\t\t\t\t\tLOGEMERG("Coinbase failed valid transaction check, aborting!");\n\t\t\t\t\texit(1);\n\t\t\t\t}\n\t\t\t}\n'''
    replace_count("src/stratifier.c", old_validation, new_validation, 2)
    replace_once(
        "src/stratifier.c",
        '\tif (wb->txns)\n\t\trealloc_strcat(&gbt_block, wb->txn_data);\n\treturn gbt_block;\n}',
        '\tif (wb->txns)\n\t\trealloc_strcat(&gbt_block, wb->txn_data);\n\tif (multichain_block_suffix())\n\t\trealloc_strcat(&gbt_block, multichain_block_suffix());\n\treturn gbt_block;\n}',
    )
    replace_once(
        "src/stratifier.c",
        '\tgenerator_preciousblock(rhash);\n',
        '\tif (multichain_preciousblock())\n\t\tgenerator_preciousblock(rhash);\n',
    )

    text = read("MULTICHAIN.md")
    marker = "## Public repository boundary\n"
    section = r'''## Peercoin serialization profile

The previously qualified keyless Peercoin path is represented without a Peercoin code branch:

```json
{
  "coinbase_txntime": true,
  "block_suffix": "00",
  "validate_coinbase": false
}
```

`coinbase_txntime` inserts the template nTime after the transaction version in the coinbase transaction. `block_suffix` appends opaque validated hex after the transaction vector; `00` is the empty trailing block-signature vector required by the qualified PoW CBlock serialization. `validate_coinbase:false` honestly skips the daemon `decoderawtransaction` startup check for transaction formats the daemon RPC does not decode; it does not fabricate a successful RPC response. Block submission and consensus acceptance remain authoritative. The default values preserve Bitcoin behavior.

`preciousblock` is also a generic boolean capability and defaults to `true`. Setting it to `false` suppresses the post-submit chain-tip hint without affecting `submitblock` itself.

'''
    if marker not in text:
        raise SystemExit("MULTICHAIN.md marker missing")
    write("MULTICHAIN.md", text.replace(marker, section + marker, 1))


MULTICHAIN_H_V2 = r'''/*
 * Generic opt-in consensus/serialization capabilities for Bitcoin-derived
 * chains. Defaults preserve upstream Bitcoin behavior.
 */
#ifndef MULTICHAIN_H
#define MULTICHAIN_H

#include <stdbool.h>
#include "yyjson.h"

struct genwork;

bool multichain_config_valid(void);
bool multichain_coinbase_txntime(void);
bool multichain_validate_coinbase(void);
bool multichain_preciousblock(void);
const char *multichain_block_suffix(void);
bool multichain_apply_gbt(struct genwork *gbt, yyjson_val *result);

#endif /* MULTICHAIN_H */
'''

MULTICHAIN_C_V2 = r'''/*
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
			uchar compact[4];

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
'''

CHAINCAPS_TEST_V2 = r'''/* Generic multichain capability and GBT extraction regression tests. */
#include "config.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ckpool.h"
#include "cashaddr.h"
#include "multichain.h"
#include "stratifier.h"
#include "yyjson.h"

ckpool_t ckpool;

static void fail(const char *msg)
{
	fprintf(stderr, "FAIL: %s\n", msg);
	exit(1);
}

static char *write_config(const char *json)
{
	char tmpl[] = "/var/tmp/ckpool-chaincaps-XXXXXX";
	int fd = mkstemp(tmpl);
	FILE *f;

	if (fd < 0)
		fail("mkstemp");
	f = fdopen(fd, "w");
	if (!f)
		fail("fdopen");
	if (fputs(json, f) < 0 || fclose(f))
		fail("write config");
	return strdup(tmpl);
}

static int run_default(void)
{
	memset(&ckpool, 0, sizeof(ckpool));
	ckpool.config = NULL;
	if (!multichain_config_valid())
		return 1;
	if (multichain_coinbase_txntime())
		return 2;
	if (!multichain_validate_coinbase())
		return 3;
	if (!multichain_preciousblock())
		return 4;
	if (multichain_block_suffix())
		return 5;
	return 0;
}

static int run_configured(void)
{
	uint8_t hash160[20] = {0};
	char address[128], config[2048], gbtjson[2048];
	struct genwork gbt;
	yyjson_doc *doc = NULL;
	yyjson_val *root;
	char *path;
	const char *suffix;
	int rc = 0;

	hash160[0] = 0x42;
	if (!cashaddr_from_hash160(address, sizeof(address), "ecash", hash160, false))
		return 10;
	snprintf(config, sizeof(config),
		"{\"cashaddr_prefix\":\"ecash\",\"coinbase_txntime\":true,"
		"\"validate_coinbase\":false,\"preciousblock\":false,"
		"\"block_suffix\":\"00\",\"gbttarget\":\"/rtt/nexttarget\","
		"\"gbtoutputs\":["
		"{\"amount\":\"/coinbasetxn/minerfund/minimumvalue\","
		"\"address\":\"/coinbasetxn/minerfund/addresses/0\"},"
		"{\"amount\":\"/coinbasetxn/stakingrewards/minimumvalue\","
		"\"script\":\"/coinbasetxn/stakingrewards/payoutscript/hex\"}]}\n");
	path = write_config(config);
	memset(&ckpool, 0, sizeof(ckpool));
	ckpool.config = path;

	if (!multichain_config_valid())
		rc = 1;
	else if (!multichain_coinbase_txntime())
		rc = 2;
	else if (multichain_validate_coinbase())
		rc = 3;
	else if (multichain_preciousblock())
		rc = 4;
	else if (!(suffix = multichain_block_suffix()) || strcmp(suffix, "00"))
		rc = 5;
	if (rc)
		goto out;

	snprintf(gbtjson, sizeof(gbtjson),
		"{\"coinbasetxn\":{\"minerfund\":{\"minimumvalue\":3200,"
		"\"addresses\":[\"%s\"]},\"stakingrewards\":{\"minimumvalue\":1000,"
		"\"payoutscript\":{\"hex\":\"51\"}}},"
		"\"rtt\":{\"nexttarget\":\"1d00ffff\"}}", address);
	doc = yyjson_read(gbtjson, strlen(gbtjson), 0);
	if (!doc) {
		rc = 6;
		goto out;
	}
	root = yyjson_doc_get_root(doc);
	memset(&gbt, 0, sizeof(gbt));
	gbt.coinbasevalue = 10000;
	gbt.diff = 9.0;
	if (!multichain_apply_gbt(&gbt, root))
		rc = 7;
	else if (gbt.mandatory_outputs != 2 ||
	         gbt.mandatory_output[0].amount != 3200 ||
	         gbt.mandatory_output[0].script_len != 25 ||
	         gbt.mandatory_output[1].amount != 1000 ||
	         gbt.mandatory_output[1].script_len != 1 ||
	         gbt.mandatory_output[1].script[0] != 0x51)
		rc = 8;
	else if (fabs(gbt.effective_diff - 1.0) > 0.000001)
		rc = 9;
out:
	if (doc)
		yyjson_doc_free(doc);
	unlink(path);
	free(path);
	return rc;
}

static void run_child(int (*fn)(void), const char *name)
{
	pid_t pid = fork();
	int status;

	if (pid < 0)
		fail("fork");
	if (!pid)
		exit(fn());
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status)) {
		fprintf(stderr, "FAIL: %s rc=%d\n", name,
		        WIFEXITED(status) ? WEXITSTATUS(status) : 255);
		exit(1);
	}
}

int main(void)
{
	run_child(run_default, "defaults");
	run_child(run_configured, "configured");
	puts("Multichain capability tests passed");
	return 0;
}
'''


def phase3():
    write("src/multichain.h", MULTICHAIN_H_V2)
    write("src/multichain.c", MULTICHAIN_C_V2)
    write("test/chaincaps_test.c", CHAINCAPS_TEST_V2)

    replace_once(
        "src/stratifier.h",
        '#ifndef STRATIFIER_H\n#define STRATIFIER_H\n\n/* Generic structure for both workbase in stratifier and gbtbase in generator */',
        '#ifndef STRATIFIER_H\n#define STRATIFIER_H\n\n#define MAX_GBT_COINBASE_OUTPUTS 8\n#define MAX_GBT_OUTPUT_SCRIPT_LEN 512\n\nstruct gbt_coinbase_output {\n\tuint64_t amount;\n\tuint16_t script_len;\n\tuint8_t script[MAX_GBT_OUTPUT_SCRIPT_LEN];\n};\n\n/* Generic structure for both workbase in stratifier and gbtbase in generator */',
    )
    replace_once(
        "src/stratifier.h",
        '\tuint64_t coinbasevalue;\n\tint height;\n\tchar *flags;',
        '\tuint64_t coinbasevalue;\n\t/* Optional consensus outputs sourced from GBT and an optional effective\n\t * difficulty distinct from the header nBits target. */\n\tint mandatory_outputs;\n\tstruct gbt_coinbase_output mandatory_output[MAX_GBT_COINBASE_OUTPUTS];\n\tdouble effective_diff; /* zero means derive from header nBits */\n\tint height;\n\tchar *flags;',
    )
    replace_once(
        "src/bitcoin.c",
        '\tgbt->height = height;\n\n\t/* The flags are optional non-consensus data appended to the coinbase',
        '\tgbt->height = height;\n\n\tif (unlikely(!multichain_apply_gbt(gbt, res_val))) {\n\t\tLOGERR("Failed to apply configured GBT consensus capabilities");\n\t\tyyjson_mut_doc_free(mut_doc);\n\t\tgoto out;\n\t}\n\n\t/* The flags are optional non-consensus data appended to the coinbase',
    )
    replace_once(
        "src/stratifier.c",
        '\tuint64_t u64, g64, d64 = 0;\n\tuint32_t u32;',
        '\tuint64_t u64, g64, d64 = 0, mandatory_total = 0;\n\tuint32_t u32;\n\tuint8_t txout_count;\n\tint i;',
    )
    replace_once(
        "src/stratifier.c",
        '''\t// Generation value\n\tg64 = wb->coinbasevalue;\n\tif (ckpool.donvalid && ckpool.donation > 0) {\n\t\tdouble dbl64 = (double)g64 / 100 * ckpool.donation;\n\n\t\td64 = dbl64;\n\t\tg64 -= d64; // To guarantee integers add up to the original coinbasevalue\n\t\twb->coinb2bin[wb->coinb2len++] = 2 + wb->insert_witness;\n\t} else\n\t\twb->coinb2bin[wb->coinb2len++] = 1 + wb->insert_witness;\n''',
        '''\t/* Generation value after any consensus-mandatory GBT outputs. */\n\tg64 = wb->coinbasevalue;\n\tfor (i = 0; i < wb->mandatory_outputs; i++) {\n\t\tmandatory_total += wb->mandatory_output[i].amount;\n\t\tif (unlikely(mandatory_total > wb->coinbasevalue)) {\n\t\t\tLOGEMERG("Mandatory GBT outputs exceed coinbasevalue");\n\t\t\texit(1);\n\t\t}\n\t}\n\tg64 -= mandatory_total;\n\ttxout_count = 1 + wb->mandatory_outputs + wb->insert_witness;\n\tif (ckpool.donvalid && ckpool.donation > 0) {\n\t\tdouble dbl64 = (double)g64 / 100 * ckpool.donation;\n\n\t\td64 = dbl64;\n\t\tg64 -= d64; // To guarantee integers add up to the original coinbasevalue\n\t\ttxout_count++;\n\t}\n\twb->coinb2bin[wb->coinb2len++] = txout_count;\n''',
    )
    replace_once(
        "src/stratifier.c",
        '\twb->coinb3bin = ckzalloc(256 + wb->insert_witness * (8 + witnessdata_size + 2));',
        '\twb->coinb3bin = ckzalloc(512 + wb->mandatory_outputs * (8 + 1 + MAX_GBT_OUTPUT_SCRIPT_LEN) +\n\t\t\t       wb->insert_witness * (8 + witnessdata_size + 2));',
    )
    replace_once(
        "src/stratifier.c",
        '''\t} else\n\t\tckpool.donation = 0;\n\n\tif (wb->insert_witness) {\n''',
        '''\t} else\n\t\tckpool.donation = 0;\n\n\tfor (i = 0; i < wb->mandatory_outputs; i++) {\n\t\tconst struct gbt_coinbase_output *output = &wb->mandatory_output[i];\n\n\t\tu64 = htole64(output->amount);\n\t\tmemcpy(wb->coinb3bin + wb->coinb3len, &u64, sizeof(uint64_t));\n\t\twb->coinb3len += sizeof(uint64_t);\n\t\t/* Configured mandatory scripts are bounded below CompactSize's 0xfd\n\t\t * threshold by MAX_GBT_OUTPUT_SCRIPT_LEN only in storage; encode the\n\t\t * full CompactSize form here so the generic path is not chain-sized. */\n\t\tif (output->script_len < 0xfd) {\n\t\t\twb->coinb3bin[wb->coinb3len++] = output->script_len;\n\t\t} else {\n\t\t\tuint16_t slen = htole16(output->script_len);\n\n\t\t\twb->coinb3bin[wb->coinb3len++] = 0xfd;\n\t\t\tmemcpy(wb->coinb3bin + wb->coinb3len, &slen, sizeof(slen));\n\t\t\twb->coinb3len += sizeof(slen);\n\t\t}\n\t\tmemcpy(wb->coinb3bin + wb->coinb3len, output->script, output->script_len);\n\t\twb->coinb3len += output->script_len;\n\t}\n\n\tif (wb->insert_witness) {\n''',
    )
    replace_once(
        "src/stratifier.c",
        '\twb->network_diff = diff_from_nbits(wb->headerbin + 72);\n',
        '\twb->network_diff = wb->effective_diff > 0 ? wb->effective_diff :\n\t\tdiff_from_nbits(wb->headerbin + 72);\n',
    )
    replace_once(
        "test/Makefile.am",
        'chaincaps_SOURCES = chaincaps_test.c $(top_srcdir)/src/multichain.c\nchaincaps_LDADD = $(top_srcdir)/src/libckpool.a',
        'chaincaps_SOURCES = chaincaps_test.c $(top_srcdir)/src/multichain.c $(top_srcdir)/src/payout.c\nchaincaps_LDADD = $(top_srcdir)/src/libckpool.a',
    )

    text = read("MULTICHAIN.md")
    marker = "## Peercoin serialization profile\n"
    section = r'''## GBT-defined coinbase outputs and effective target

`gbtoutputs` is an optional array of generic descriptors. Each entry supplies an RFC6901-style JSON pointer in `amount` and exactly one pointer in `address` or `script`. `optional:true` permits a descriptor to disappear from a template; otherwise missing configured data rejects that template. Address outputs use the configured payout codec, including `cashaddr_prefix` when present. Script outputs consume raw scriptPubKey hex from GBT. All configured amounts are subtracted from the miner/pool generation value and emitted as separate transaction outputs before any witness commitment.

`gbttarget` is an optional JSON pointer to a compact 4-byte target string. When present it replaces the effective block-solve/network difficulty used by ckpool while leaving the actual header nBits untouched. If the pointed field is absent on a particular template, normal GBT difficulty is retained.

The current eCash profile is therefore data only:

```json
{
  "cashaddr_prefix": "ecash",
  "gbtoutputs": [
    {
      "amount": "/coinbasetxn/minerfund/minimumvalue",
      "address": "/coinbasetxn/minerfund/addresses/0"
    },
    {
      "amount": "/coinbasetxn/stakingrewards/minimumvalue",
      "script": "/coinbasetxn/stakingrewards/payoutscript/hex"
    }
  ],
  "gbttarget": "/rtt/nexttarget",
  "preciousblock": false
}
```

This matches the ordinary GBT shape used by the Bitcoin ABC ckpool reference. Installations using another GBT shape, including simple-GBT script fields, can point the same generic descriptors at those fields without adding coin-specific C code.

'''
    if marker not in text:
        raise SystemExit("MULTICHAIN.md PPC marker missing")
    write("MULTICHAIN.md", text.replace(marker, section + marker, 1))


if __name__ == "__main__":
    if len(sys.argv) != 2 or sys.argv[1] not in {"phase1", "phase2", "phase3"}:
        raise SystemExit("usage: agent_multichain_complete.py phase1|phase2|phase3")
    globals()[sys.argv[1]]()
