#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cashaddr.h"

static void expect_p2pkh(const char *addr, const char *prefix)
{
    uint8_t script[64];
    bool p2sh = true;
    int len = cashaddr_to_script(addr, prefix, script, &p2sh);

    assert(len == 25);
    assert(!p2sh);
    assert(script[0] == 0x76);
    assert(script[1] == 0xa9);
    assert(script[2] == 0x14);
    assert(script[23] == 0x88);
    assert(script[24] == 0xac);
}

int main(void)
{
    char normalized[196];

    expect_p2pkh("bitcoincash:qqwytctx80gcz65xrhmd45qw68stwzedxvat6jnqrp", "bitcoincash");
    expect_p2pkh("qqwytctx80gcz65xrhmd45qw68stwzedxvat6jnqrp", "bitcoincash");
    expect_p2pkh("ecash:qzph75apzfr0zty0sjuzzeywrrtfx8hgzswrxvadel", "ecash");

    assert(cashaddr_normalize(normalized, sizeof(normalized),
        "QQWYTCTX80GCZ65XRHMD45QW68STWZEDXVAT6JNQRP", "bitcoincash"));
    assert(strcmp(normalized,
        "bitcoincash:qqwytctx80gcz65xrhmd45qw68stwzedxvat6jnqrp") == 0);

    assert(!cashaddr_to_script(
        "bitcoincash:qqwytctx80gcz65xrhmd45qw68stwzedxvat6jnqrp",
        "ecash", (uint8_t *)normalized, NULL));

    puts("PASS: cashaddr");
    return 0;
}
