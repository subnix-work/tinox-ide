# Semantic highlighting (#29)

Tinox buffers layer semantic colours over their syntax highlighting when the
language server advertises full semantic tokens. Opening a buffer or editing it
schedules a full request after 180 ms of quiet. Other editor features and typing
remain available while a request is outstanding. Servers without the capability
keep ordinary syntax highlighting without an error message.

Settings → Semantic Colours exposes an optional `#RRGGBB` override for every
standard LSP token type. Leave a field empty to follow the editor theme. Changes
apply to open buffers automatically. Types use the theme's DataType role, methods
and functions use Function, fields use Attribute, namespaces use Import, parameters
use Variable and enum members use Constant. If Attribute matches DataType or
Function, fields use another distinct colour from the same theme. Other standard
types use the corresponding syntax roles; unknown types use Normal. Token
modifiers are accepted but do not alter colours. Theme changes update existing
ranges immediately, retaining explicit colour overrides. Split views share the
IDE's configured editor theme and the document's semantic ranges.

The controller uses the server's legend rather than assuming token indices. Full
results must match the captured document version; the Qt command checks that
version again before applying. Packed token data, integer bounds, ordering,
nonoverlap, document bounds and UTF-16 surrogate boundaries are validated before
replacing the complete overlay. Malformed or failed replies preserve the previous
ranges. A successful null or empty result clears them. An edit cancels obsolete
requests but keeps the previous moving ranges until a valid new result arrives,
so colours follow inserted/removed text without flashing back to syntax colours.
Close, reload and server retirement remove ranges while the document is alive.
The overlay changes foreground only and leaves diagnostics and undo history intact.

The controller processes one URI per helper call, using early returns to avoid
`continue` in for-in loops on installed Tinox 2.8.0 compilers
([tinox#513](https://github.com/subnix-work/tinox/issues/513)). The compiler fix
exists upstream; this also keeps debounce and response handling independently
readable.

`SemanticHighlightingTest` covers debounce, cancellation, unsupported servers,
colour validation, stale replies, failures and shutdown. Native editor tests
verify atomic validation, surrogate boundaries, distinct Breeze colours, theme
changes, overrides, moving ranges and release. `scripts/test-semantics.sh` runs the
real Qt bridge and tinox-lsp to check classification, typing and live preferences.

Frequent semantic requests exposed a separate runtime Map churn bug
([tinox#581](https://github.com/subnix-work/tinox/issues/581)): after deleted slots
fill the table, missing-key lookups can loop forever. Pending RPC and semantic
maps are rebuilt when retiring an entry until a corrected runtime is available.
`LspTest.testRequestChurn` exercises 100 completed/cancelled requests, late replies
and an unrelated outstanding request; the real editing smoke covers the original
combined formatting/semantic scenario.
