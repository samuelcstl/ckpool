# Peercoin ckpool testnet qualification

Date: 2026-09-10

## Result

HARD PASS

A physical NerdQAxe++ using 4x BM1370 ASICs mined Peercoin testnet
through this keyless ckpool implementation.

51 consecutive final active-chain blocks were produced.

First qualified block:

- height: 699162
- hash: 0000000000329aeaab6731ebac159f70c6a7606dbc2d28212399739fb0b19e6d

Last qualified block:

- height: 699212
- hash: 000000000036c888501412f003e12e9ac81f6348e55fb6fee9e2f5d42d6bf01d

Qualified active-chain range:

- 699162 through 699212
- 51 blocks

Every qualifying block independently matched:

- the configured Peercoin payout script
- the coinbase tag /NerdQAxe-PPC/

Peercoin Core accepted rolled block versions produced through Stratum
version rolling mask 1fffe000.

The test therefore qualifies:

- Peercoin transaction nTime handling
- Peercoin coinbase construction
- Stratum V1 subscribe / authorize / notify
- Stratum version rolling
- BM1370 rolled-version work
- merkle/header reconstruction
- Peercoin CBlock serialization
- empty trailing vchBlockSig for current PoW
- submitblock
- Peercoin consensus acceptance
- ZMQ new-block handling
- keyless payout architecture

## Security result

ckpool does not require or contain the payout private key.

The original reference fork's blocksignkey architecture is deliberately
not used.

The payout private key remains wallet/recovery material only.

## Scope

This qualifies the Peercoin protocol implementation on testnet.

Mainnet operational qualification, fresh wallet recovery and real
mainnet ASIC shares remain separate gates.
