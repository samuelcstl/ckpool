/*
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
