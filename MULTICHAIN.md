# Multichain compatibility

This fork keeps current upstream ckpool behavior as the default and represents Bitcoin-derived chain differences as explicit protocol capabilities and configuration rather than coin-name conditionals wherever practical.

## Deployed provenance baseline

The multichain work is grounded in pool implementations already deployed in the local mining lanes, not in hypothetical chain support.

| Chain | Proven deployed pool lineage | Deployed revision | Local source hacks | What this means for the unified fork |
| --- | --- | --- | --- | --- |
| BTC | upstream `ckolivas/ckpool` | `c26eb7ff` | none | upstream behavior is the default contract |
| DGB | upstream `ckolivas/ckpool` | same binary as BTC | none | no DGB-specific source behavior is required by the deployed lane |
| AUR | upstream `ckolivas/ckpool` | same binary as BTC | none | no AUR-specific source behavior is required |
| BFX | upstream `ckolivas/ckpool` | same binary as BTC | none | no BFX-specific source behavior is required |
| BCH | `skaisser/ckpool` | `0479f860` | no meaningful local edits | port the BCH fork's protocol requirements as generic capabilities where possible |
| XEC | `Bitcoin-ABC/ecash-ckpool-solo` | `10bcb1ca` | no meaningful local edits | port mandatory eCash coinbase/RTT/address behavior without changing BTC defaults |
| PPC | qualified keyless PPC ckpool lineage retained in this repository | qualification tip `9bfb38a9` | qualified source delta | port transaction/block serialization differences as explicit capabilities |
| LCC | not yet deployed | n/a | n/a | first new lane; SHA256d GBT selection is the immediate requirement |

The unified branch is based on current upstream source, while the previously qualified PPC lineage remains in Git ancestry and its qualification documents remain in-tree.

## getblocktemplate fixed parameters

Three generic configuration keys cover fixed `getblocktemplate` request variation:

- `gbtparams`: an optional JSON object merged into the standard first BIP22/BIP23 template-request object. Configured keys replace the built-in value with the same key.
- `gbtdrop`: an optional array of field names removed from that first template-request object after `gbtparams` is applied.
- `gbtargs`: an optional JSON array whose values are appended as additional JSON-RPC positional parameters after the first template-request object.

The operations are deliberately generic. They correspond to add/replace, remove, and append rather than to named chains. The resolved GBT request is cached once per ckpool process because runtime configuration is startup-static.

### Bitcoin, DigiByte, Auroracoin and Bitfinite deployed lanes

No override is required by the currently deployed ckpool instances:

```json
{}
```

DigiByte nodes can expose an algorithm positional argument in their RPC interface. If an installation needs to select it explicitly, the generic representation is available without a DigiByte source branch:

```json
{
  "gbtargs": ["sha256d"]
}
```

This is an available configuration mechanism, not a claim that the currently deployed DGB lane requires it.

### Litecoin Cash SHA256d

The LCC node requires a fixed `powalgo` field for SHA256d template selection:

```json
{
  "gbtparams": {
    "powalgo": "sha256d"
  }
}
```

### Bitcoin Cash GBT

The deployed BCH fork requests a template without the SegWit `rules` member. The generic equivalent is:

```json
{
  "gbtdrop": ["rules"]
}
```

`gbtparams` and `gbtargs` preserve JSON value types, so future chains may provide booleans, numbers, strings, arrays, objects, or null values without adding chain-specific code. `gbtdrop` entries must be strings.

## Capability inventory before fleet qualification

The fork is not considered fleet-ready until every consensus- or payout-relevant delta from the proven implementations is represented and tested.

| Capability | BTC/DGB/AUR/BFX | LCC | PPC | BCH | XEC |
| --- | --- | --- | --- | --- | --- |
| standard upstream GBT | default | plus fixed param | default | drop `rules` | default request shape |
| configurable fixed GBT data | supported | `powalgo=sha256d` | available | remove `rules` | available |
| standard Bitcoin transaction serialization | yes | yes | no | yes | yes |
| transaction `nTime` | no | no | required | no | no |
| trailing block signature vector | no | no | required empty vector | no | no |
| Base58 payout scripts | yes | yes | required | legacy supported | legacy supported |
| CashAddr payout scripts | no | no | no | required | required |
| mandatory GBT-defined coinbase outputs | no | no | no | no | miner fund + staking rewards |
| alternate next-block target from GBT | no | no | no | no | RTT target |
| suppress post-submit `preciousblock` | no | no | no | no | required for Avalanche compatibility |
| strict negotiated SV1 version mask | hardening target | important (`0000e000`) | important | useful | useful |

The remaining implementation work therefore belongs to four generic layers:

1. **GBT request shaping**: already implemented through `gbtparams`, `gbtdrop`, and `gbtargs`.
2. **Address/script codecs**: add configurable CashAddr support shared by BCH and XEC while preserving normal Base58/SegWit behavior by default.
3. **Coinbase/block capabilities**: optional transaction timestamp, optional block suffix, and configurable mandatory outputs sourced from GBT.
4. **Mining semantics**: selectable target source, selectable post-submit chain-tip behavior, and strict per-client SV1 version-mask negotiation/reconstruction.

## Peercoin serialization profile

The previously qualified keyless Peercoin path is represented without a Peercoin code branch:

```json
{
  "coinbase_txntime": true,
  "block_suffix": "00",
  "validate_coinbase": false
}
```

`coinbase_txntime` inserts the template nTime after the transaction version in the coinbase transaction. `block_suffix` appends opaque validated hex after the transaction vector; `00` is the empty trailing block-signature vector required by the qualified PoW CBlock serialization. `validate_coinbase:false` honestly skips the daemon `decoderawtransaction` startup check for transaction formats the daemon RPC does not decode; it does not fabricate a successful RPC response. Block submission and consensus acceptance remain authoritative. The default values preserve Bitcoin behavior.

`preciousblock` is also a generic boolean capability and defaults to `true`. Setting it to `false` suppresses the post-submit chain-tip hint without affecting `submitblock` itself.

## Public repository boundary

These settings contain protocol configuration only. Runtime credentials, payout private keys, private network addresses, node cookies, host-specific paths, live service configuration and recovery material must not be committed to this public repository. This repository must never use self-hosted GitHub Actions runners.

## Design rule

Prefer a generic configurable representation whenever a protocol difference can be described as data or an orthogonal capability. Do not use coin tickers as switches in common code when an explicit capability can express the same requirement. A tightly coupled consensus feature may have its own named capability only when decomposing it would make correctness harder to reason about.
