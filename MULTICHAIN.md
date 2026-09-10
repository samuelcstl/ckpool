# Multichain compatibility

This fork keeps Bitcoin-compatible behavior as the default and adds chain-specific protocol differences as explicit configuration rather than coin-name conditionals.

## getblocktemplate fixed parameters

Three generic configuration keys cover the known forms of chain-specific `getblocktemplate` variation:

- `gbtparams`: an optional JSON object merged into the standard first BIP22/BIP23 template-request object. Configured keys replace the built-in value with the same key.
- `gbtdrop`: an optional array of field names removed from that first template-request object after `gbtparams` is applied.
- `gbtargs`: an optional JSON array whose values are appended as additional JSON-RPC positional parameters after the first template-request object.

The operations are deliberately generic. They correspond to add/replace, remove, and append rather than to named chains.

Examples:

```json
// Bitcoin: no override required
{}
```

```json
// Litecoin Cash SHA256d: add a fixed field to the template request
{
  "gbtparams": {
    "powalgo": "sha256d"
  }
}
```

```json
// DigiByte SHA256d: append the node's algorithm positional argument
{
  "gbtargs": ["sha256d"]
}
```

```json
// Bitcoin Cash style GBT: omit the SegWit rules declaration
{
  "gbtdrop": ["rules"]
}
```

`gbtparams` and `gbtargs` preserve JSON value types, so future chains may provide booleans, numbers, strings, arrays, objects, or null values without adding chain-specific code. `gbtdrop` entries must be strings.

These settings contain protocol configuration only. Runtime credentials, payout keys, private network addresses, node cookies, and host-specific paths must not be committed to this public repository.

## Design rule

Prefer a generic configurable representation of a protocol difference whenever the difference can be expressed as fixed request data or serialization behavior. Coin tickers must not be used as switches in common code when an explicit capability can express the same requirement.
