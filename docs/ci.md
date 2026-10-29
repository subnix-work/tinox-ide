# Continuous integration

Issue #34. `.github/workflows/ci.yml` runs on every pull request, on pushes to `main`, on version tags
(`v*`), nightly and on demand. `packaging.yml` (the Flatpak build and smoke test) is separate.

All jobs run in an `archlinux:latest` container: Arch ships Qt 6 and KDE Frameworks 6 at the versions the
IDE is developed against, Ubuntu 24.04 has neither. The shared setup is the composite action
`.github/actions/setup`: it installs the packages of [development.md](development.md), checks out the
pinned Tinox compiler and builds it with cargo, runs `tinox install` for the IDE's own dependency, and
sets `TINOX`, `TINOX_LSP`, ccache and `QT_QPA_PLATFORM=offscreen`.

## Jobs

| Job | What it does |
|---|---|
| `lint` | `scripts/check-format.sh` (clang-format for `bridge/`, `tinox fmt` for the Tinox sources), `scripts/check-docs.py`, `scripts/test-check-env.sh`, `scripts/test-syntax.sh` |
| `bridge` | builds the bridge and runs the whole suite offscreen (`make test-bridge`); uploads `libtinoxkde*` for the later jobs |
| `tinox-tests` | six shards; each runs its share of `tests/*Test.tnx`, one file at a time, against the bridge mock (`scripts/ci-tinox-tests.sh`). A whole-workspace `tinox test` is too slow (#63) and hides which file failed |
| `e2e` | the real IDE on Qt's offscreen platform: example, diagnostics, navigation, editing, semantics, debugger (real gdb), accessibility, file-scoped test discovery |
| `ide` | `tinox build` of the IDE, packed with the bridge as `tinox-ide-linux-x86_64.tar.gz` (artifact) |
| `ci-ok` | fails if any job above did not succeed; require this one job in the branch rules |
| `release` | on a `v*` tag: attaches the tarball to a GitHub release. The Flatpak bundle is an artifact of `packaging.yml` |
| `nightly` | schedule and manual runs only: `make test-asan`, `make test-tsan`, `make fuzz FUZZ_SECONDS=600` (a privileged container for TSan's address layout) |

## The pinned Tinox compiler

`.github/tinox-ref` holds the commit of `subnix-work/tinox` the CI builds. Change it in the same pull request
that needs a newer compiler; the cargo build, `~/.cargo` and `~/.tinox` are cached under that commit, so a
change costs one cold build. Keep it in step with the commit `packaging/org.subnix.tinoxide.json` builds.

## Running the same checks locally

```sh
scripts/check-format.sh                 # lint, formatting part
make test-bridge                        # bridge job
scripts/ci-tinox-tests.sh 0 6           # one tinox-tests shard
make test                               # everything, including the e2e smokes
```

## Gating

Branch protection (required checks) is a repository setting and not available for private repositories on
the current GitHub plan, so it cannot be switched on from here. When it is available, require `ci-ok`.
There is no AppImage: the Flatpak bundle is the portable artifact.
