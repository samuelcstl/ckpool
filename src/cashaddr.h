/*
 * Generic CashAddr decoding helpers for Bitcoin-derived chains.
 *
 * The caller supplies the expected human-readable prefix. The codec contains
 * no chain-name policy and supports hash160 P2PKH/P2SH payloads only.
 */

#ifndef CKPOOL_CASHADDR_H
#define CKPOOL_CASHADDR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Validate a CashAddr against expected_prefix and extract its hash160/type. */
bool cashaddr_decode(const char *addr, const char *expected_prefix,
                     uint8_t hash160[20], bool *is_p2sh);

/* Convert a validated CashAddr directly to its scriptPubKey. */
int cashaddr_to_script(const char *addr, const char *expected_prefix,
                       uint8_t *script, bool *is_p2sh);

/*
 * Prefer CashAddr decoding when expected_prefix is configured, otherwise
 * preserve ckpool's existing Base58/SegWit script construction verbatim.
 */
int cashaddr_or_standard_to_script(uint8_t *script, const char *addr,
                                   const char *expected_prefix,
                                   bool standard_script, bool segwit);

/*
 * Canonicalise a valid address to lowercase "prefix:payload" form.
 * Returns false if the address is invalid or the destination is too small.
 */
bool cashaddr_normalize(char *dst, size_t dst_len, const char *addr,
                        const char *expected_prefix);

#endif /* CKPOOL_CASHADDR_H */
