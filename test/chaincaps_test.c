/* Generic multichain capability and GBT extraction regression tests. */
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
	const char *address = "ecash:qpm2qsznhks23z7629mms6s4cwef74vcwva87rkuu2";
	char config[2048], gbtjson[2048];
	struct genwork gbt;
	yyjson_doc *doc = NULL;
	yyjson_val *root;
	char *path;
	const char *suffix;
	int rc = 0;

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
