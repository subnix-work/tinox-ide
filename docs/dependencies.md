# Documentation dependencies

The IDE currently declares the following external Tinox module. Run `tinox install`
from the checkout when preparing a development toolchain; use the compiler/core/runtime
revision documented in [development](development.md).

## JSON 1.1.0

Coordinate: `tinox.core:json:1.1.0`. The IDE uses `Json`, `JsonValue` and `JsonField`
for command/event payloads and protocol/configuration data. The declared version is
in `tinox.toml`; the Flatpak build verifies its registry envelope and archive hashes.
[Registry metadata](https://central.tinox-lang.de/api/v1/tinox.core/json/1.1.0)
identifies the immutable artifact.

`tinox doc` currently assumes a published stdlib directory layout when linking
dependency documentation, even for an arbitrary local output directory
([tinox#586](https://github.com/subnix-work/tinox/issues/586)). The local docs builder
redirects that one known broken JSON link here. This is dependency provenance and
usage guidance, not a generated JSON API. Unknown/broken local links still fail the
build, so a future dependency/version needs an explicit update.
