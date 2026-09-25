/*
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
