# Multichain compatibility

This fork keeps Bitcoin-compatible behavior as the default and adds chain-specific protocol differences as explicit configuration rather than coin-name conditionals.

## getblocktemplate fixed parameters

Two generic configuration keys cover the known forms of chain-specific `getblocktemplate` selection:

- `gbtparams`: an optional JSON object merged into the standard first BIP22/BIP23 template-request object. Configured keys replace the built-in value with the same key.
- `gbtargs`: an optional JSON array whose values are appended as additional JSON-RPC positional parameters after the first template-request object.

Examples:

```json
// Bitcoin: no override required
{}
```

```json
// Litecoin Cash SHA256d
{
  "gbtparams": {
    "powalgo": "sha256d"
  }
}
```

```json
// DigiByte SHA256d
{
  "gbtargs": ["sha256d"]
}
```

The implementation must preserve JSON value types, so future chains may provide booleans, numbers, arrays, objects, or null values without adding chain-specific code.

These settings contain protocol configuration only. Runtime credentials, payout keys, private network addresses, node cookies, and host-specific paths must not be committed to this public repository.

## Design rule

Prefer a generic configurable representation of a protocol difference whenever the difference can be expressed as fixed request data or serialization behavior. Coin tickers must not be used as switches in common code when an explicit capability can express the same requirement.
