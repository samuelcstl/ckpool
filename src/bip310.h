/*
 * BIP310 version-rolling helpers.
 *
 * These helpers implement the protocol arithmetic only. Session state and
 * JSON-RPC handling live in the stratifier.
 */

#ifndef CKPOOL_BIP310_H
#define CKPOOL_BIP310_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BIP310_FULL_MASK 0xffffffffU

/* TMask is exactly eight hexadecimal characters, case-insensitive. */
bool bip310_parse_mask(const char *mask, uint32_t *out);
void bip310_format_mask(char out[9], uint32_t mask);

/* Server response mask is the intersection of server and miner masks. */
static inline uint32_t bip310_negotiate_mask(uint32_t server_mask,
                                             uint32_t miner_mask)
{
	return server_mask & miner_mask;
}

/* A submit may only modify bits allowed by the connection's last mask. */
static inline bool bip310_version_bits_valid(uint32_t version_bits,
                                             uint32_t last_mask)
{
	return (version_bits & ~last_mask) == 0;
}

/* BIP310 replacement semantics, not OR semantics. */
static inline uint32_t bip310_apply_version(uint32_t job_version,
                                            uint32_t last_mask,
                                            uint32_t version_bits)
{
	return (job_version & ~last_mask) | (version_bits & last_mask);
}

unsigned int bip310_popcount(uint32_t mask);

#endif /* CKPOOL_BIP310_H */
