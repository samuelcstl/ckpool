/* End-to-end configuration profile regressions for the generic multichain layer. */
#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ckpool.h"
#include "bitcoin.h"
#include "multichain.h"
#include "stratifier.h"
#include "yyjson.h"

ckpool_t ckpool;

static char *write_config(const char *json)
{
    char tmpl[] = "/var/tmp/ckpool-profile-XXXXXX";
    int fd = mkstemp(tmpl);
    FILE *f;

    if (fd < 0)
        return NULL;
    f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        unlink(tmpl);
        return NULL;
    }
    if (fputs(json, f) < 0 || fclose(f)) {
        unlink(tmpl);
        return NULL;
    }
    return strdup(tmpl);
}

static void cleanup_config(char *path)
{
    if (!path)
        return;
    unlink(path);
    free(path);
}

static int profile_default(void)
{
    memset(&ckpool, 0, sizeof(ckpool));
    if (!multichain_config_valid())
        return 1;
    if (multichain_coinbase_txntime() || !multichain_validate_coinbase() ||
        !multichain_preciousblock() || multichain_block_suffix())
        return 2;
    return 0;
}

static int profile_ppc(void)
{
    char *path = write_config("{\"coinbase_txntime\":true,\"block_suffix\":\"00\",\"validate_coinbase\":false}\n");
    const char *suffix;
    int rc = 0;

    if (!path)
        return 1;
    memset(&ckpool, 0, sizeof(ckpool));
    ckpool.config = path;
    if (!multichain_config_valid())
        rc = 2;
    else if (!multichain_coinbase_txntime() || multichain_validate_coinbase())
        rc = 3;
    else if (!multichain_preciousblock())
        rc = 4;
    else if (!(suffix = multichain_block_suffix()) || strcmp(suffix, "00"))
        rc = 5;
    cleanup_config(path);
    return rc;
}

static int profile_bch(void)
{
    static const char *addr = "bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a";
    static const char *prefixless = "qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a";
    static const char *p2sh = "bitcoincash:ppm2qsznhks23z7629mms6s4cwef74vcwvn0h829pq";
    char script[64];
    bool is_script = false, segwit = true;
    char *path = write_config("{\"cashaddr_prefix\":\"bitcoincash\",\"gbtdrop\":[\"rules\"]}\n");
    int len, rc = 0;

    if (!path)
        return 1;
    memset(&ckpool, 0, sizeof(ckpool));
    ckpool.config = path;
    if (!multichain_config_valid())
        rc = 2;
    else if (!payout_address_is_cashaddr(addr, &is_script, &segwit) || is_script || segwit)
        rc = 3;
    else if (!payout_address_is_cashaddr(prefixless, &is_script, &segwit) || is_script || segwit)
        rc = 4;
    else if (!payout_address_is_cashaddr(p2sh, &is_script, &segwit) || !is_script || segwit)
        rc = 5;
    else if (payout_address_is_cashaddr("ecash:qpm2qsznhks23z7629mms6s4cwef74vcwva87rkuu2",
                                        &is_script, &segwit))
        rc = 6;
    else if ((len = payout_address_to_txn(script, addr, false, false)) != 25)
        rc = 7;
    else if ((unsigned char)script[0] != 0x76 || (unsigned char)script[1] != 0xa9 ||
             (unsigned char)script[2] != 0x14 || (unsigned char)script[23] != 0x88 ||
             (unsigned char)script[24] != 0xac)
        rc = 8;
    cleanup_config(path);
    return rc;
}

static int profile_xec(void)
{
    static const char *addr = "ecash:qpm2qsznhks23z7629mms6s4cwef74vcwva87rkuu2";
    char config[2048], gbtjson[2048];
    struct genwork gbt;
    yyjson_doc *doc = NULL;
    yyjson_val *root;
    char *path;
    int rc = 0;

    snprintf(config, sizeof(config),
        "{\"cashaddr_prefix\":\"ecash\",\"preciousblock\":false,"
        "\"gbttarget\":\"/rtt/nexttarget\",\"gbtoutputs\":["
        "{\"amount\":\"/coinbasetxn/minerfund/minimumvalue\","
        "\"address\":\"/coinbasetxn/minerfund/addresses/0\"},"
        "{\"amount\":\"/coinbasetxn/stakingrewards/minimumvalue\","
        "\"script\":\"/coinbasetxn/stakingrewards/payoutscript/hex\"}]}\n");
    path = write_config(config);
    if (!path)
        return 1;
    memset(&ckpool, 0, sizeof(ckpool));
    ckpool.config = path;
    if (!multichain_config_valid()) {
        rc = 2;
        goto out;
    }
    if (multichain_coinbase_txntime() || !multichain_validate_coinbase() ||
        multichain_preciousblock() || multichain_block_suffix()) {
        rc = 3;
        goto out;
    }
    {
        bool is_script = true, segwit = true;
        if (!payout_address_is_cashaddr(addr, &is_script, &segwit) || is_script || segwit) {
            rc = 8;
            goto out;
        }
    }
    snprintf(gbtjson, sizeof(gbtjson),
        "{\"coinbasetxn\":{\"minerfund\":{\"minimumvalue\":3200,"
        "\"addresses\":[\"%s\"]},\"stakingrewards\":{\"minimumvalue\":1000,"
        "\"payoutscript\":{\"hex\":\"51\"}}},\"rtt\":{\"nexttarget\":\"1d00ffff\"}}",
        addr);
    doc = yyjson_read(gbtjson, strlen(gbtjson), 0);
    if (!doc) {
        rc = 4;
        goto out;
    }
    root = yyjson_doc_get_root(doc);
    memset(&gbt, 0, sizeof(gbt));
    gbt.coinbasevalue = 10000;
    gbt.diff = 9.0;
    if (!multichain_apply_gbt(&gbt, root))
        rc = 5;
    else if (gbt.mandatory_outputs != 2 ||
             gbt.mandatory_output[0].amount != 3200 ||
             gbt.mandatory_output[0].script_len != 25 ||
             gbt.mandatory_output[1].amount != 1000 ||
             gbt.mandatory_output[1].script_len != 1 ||
             gbt.mandatory_output[1].script[0] != 0x51)
        rc = 6;
    else if (fabs(gbt.effective_diff - 1.0) > 0.000001)
        rc = 7;
out:
    if (doc)
        yyjson_doc_free(doc);
    cleanup_config(path);
    return rc;
}

static int profile_target_fallback(void)
{
    struct genwork gbt;
    yyjson_doc *doc;
    char *path = write_config("{\"gbttarget\":\"/rtt/nexttarget\"}\n");
    int rc = 0;

    if (!path)
        return 1;
    memset(&ckpool, 0, sizeof(ckpool));
    ckpool.config = path;
    doc = yyjson_read("{}", 2, 0);
    if (!doc)
        rc = 2;
    else {
        memset(&gbt, 0, sizeof(gbt));
        gbt.coinbasevalue = 1;
        gbt.diff = 7.0;
        if (!multichain_apply_gbt(&gbt, yyjson_doc_get_root(doc)))
            rc = 3;
        else if (fabs(gbt.effective_diff - 7.0) > 0.000001)
            rc = 4;
        yyjson_doc_free(doc);
    }
    cleanup_config(path);
    return rc;
}

static int profile_max_script(void)
{
    char script_hex[MAX_GBT_OUTPUT_SCRIPT_LEN * 2 + 1];
    char *gbtjson = NULL, *path;
    struct genwork gbt;
    yyjson_doc *doc = NULL;
    int i, rc = 0;

    path = write_config("{\"gbtoutputs\":[{\"amount\":\"/required/value\",\"script\":\"/required/script\"}]}\n");
    if (!path)
        return 1;
    for (i = 0; i < MAX_GBT_OUTPUT_SCRIPT_LEN; i++) {
        script_hex[i * 2] = '5';
        script_hex[i * 2 + 1] = '1';
    }
    script_hex[sizeof(script_hex) - 1] = '\0';
    if (asprintf(&gbtjson, "{\"required\":{\"value\":1,\"script\":\"%s\"}}", script_hex) < 0) {
        cleanup_config(path);
        return 2;
    }
    memset(&ckpool, 0, sizeof(ckpool));
    ckpool.config = path;
    doc = yyjson_read(gbtjson, strlen(gbtjson), 0);
    if (!doc)
        rc = 3;
    else {
        memset(&gbt, 0, sizeof(gbt));
        gbt.coinbasevalue = 2;
        gbt.diff = 1.0;
        if (!multichain_apply_gbt(&gbt, yyjson_doc_get_root(doc)))
            rc = 4;
        else if (gbt.mandatory_outputs != 1 ||
                 gbt.mandatory_output[0].script_len != MAX_GBT_OUTPUT_SCRIPT_LEN ||
                 gbt.mandatory_output[0].script[MAX_GBT_OUTPUT_SCRIPT_LEN - 1] != 0x51)
            rc = 5;
        yyjson_doc_free(doc);
    }
    free(gbtjson);
    cleanup_config(path);
    return rc;
}

static void run_child(int (*fn)(void), const char *name)
{
    pid_t pid = fork();
    int status;

    if (pid < 0) {
        fprintf(stderr, "FAIL: fork %s\n", name);
        exit(1);
    }
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
    /* These five lanes intentionally share Bitcoin-default consensus capabilities. */
    run_child(profile_default, "btc-default");
    run_child(profile_default, "dgb-default-caps");
    run_child(profile_default, "aur-default-caps");
    run_child(profile_default, "bfx-default-caps");
    run_child(profile_default, "lcc-default-caps");
    run_child(profile_bch, "bch-cashaddr");
    run_child(profile_ppc, "ppc-serialization");
    run_child(profile_xec, "xec-gbt-consensus");
    run_child(profile_target_fallback, "target-fallback");
    run_child(profile_max_script, "max-script-boundary");
    puts("Eight-chain profile regressions passed");
    return 0;
}
