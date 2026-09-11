/* Generic multichain capability configuration regression tests. */
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
