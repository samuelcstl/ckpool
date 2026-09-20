#ifndef COINBASE_EXTENSION_H
#define COINBASE_EXTENSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yyjson.h"

#define COINBASE_EXTENSION_MAX_OUTPUTS 8
#define COINBASE_EXTENSION_MAX_SCRIPT_BYTES 252

typedef struct {
	uint64_t value;
	unsigned char script[COINBASE_EXTENSION_MAX_SCRIPT_BYTES];
	size_t script_len;
} coinbase_extension_output_t;

typedef struct {
	uint64_t miner_value;
	size_t output_count;
	coinbase_extension_output_t outputs[COINBASE_EXTENSION_MAX_OUTPUTS];
} coinbase_extension_plan_t;

/* Return true for supported explicit extension names. */
bool coinbase_extension_type_valid(const char *type);

/*
 * Build the additional consensus output plan for a GBT. The caller must only
 * invoke this when an extension is explicitly configured. Adapters fail closed
 * on missing or malformed consensus metadata.
 */
bool coinbase_extension_plan(const char *type, yyjson_val *gbt,
			     uint64_t coinbase_value,
			     coinbase_extension_plan_t *plan);

/* Serialize one planned txout: value, compact one-byte script length, script. */
size_t coinbase_extension_serialize_output(unsigned char *out, size_t capacity,
					   const coinbase_extension_output_t *output);

#endif
