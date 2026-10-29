# Dogfooding friction log

Reviewed: **2026-10-29**, documentation/self-hosting checkpoint for issue #35.
Statuses below are a dated snapshot, not a promise that every later toolchain has
the same behaviour. Update the row after testing the actual replacement version.

## Reporting and review rule

For each pain point encountered while developing the IDE, create or reuse an issue:
IDE/bridge/UI problems belong in `subnix-work/tinox-ide`; compiler/runtime/stdlib/LSP
problems belong in `subnix-work/tinox`. Record the observed operation, reproduction
or evidence, toolchain identity, current accommodation and exit condition here.
Do not silently work around it or treat an upstream closure as proof the installed
binary has the fix.

Before advancing **each milestone**, review all open rows, reproduce or retire
accommodations, check issue status/release availability and record the result in the
review table. Carry remaining issues explicitly into the next milestone. Resolved
rows remain as history with their regression test/evidence.

## Current observations and tracked constraints

| Operation / friction | Owning issue | Current handling and exit condition |
|---|---|---|
| Full-project Check on the IDE's connected graph still ran after over 60 seconds in the offscreen repository-copy run; stopped that run. Other compiler tests were active, so this is not a standalone benchmark. | [tinox#559](https://github.com/subnix-work/tinox/issues/559), open | The IDE preset checks the real entry/imports; full-project configuration remains available. Remove the interactive accommodation after measured whole-project feedback improves. |
| Workspace-wide Discover Tests exceeded the automated run's 30-second budget after the IDE had checked and rebuilt/run successfully. | [IDE#63](https://github.com/subnix-work/tinox-ide/issues/63), open | The bounded self-hosting test supplies a real file-scoped list step to the same runner; no UI file-discovery command is claimed. Add that command and verify full discovery/cancellation separately. |
| Flags before a positional test file silently broaden discovery/execution to the whole workspace; two independent one-test files reproduce 2 discovered versus 1 when reordered. | [tinox#585](https://github.com/subnix-work/tinox/issues/585), open | The IDE passes the selected file before flags. The self-hosting regression requires exactly three selected-file cases from the much larger IDE suite. |
| Generated local API documentation points its JSON dependency link at a missing published-site path. | [tinox#586](https://github.com/subnix-work/tinox/issues/586), open | Redirect the known URL to [dependency provenance](dependencies.md); keep validation strict for unknown links. Retire after the compiler supports relocatable dependency documentation. |
| A selected method runs other tests in its file; failed reruns also have file scope. | [tinox#582](https://github.com/subnix-work/tinox/issues/582), open | The manual names the file granularity. Switch only after the compiler advertises exact filtering and regressions cover unknown filters. |
| The installed 2.8.0 development binary still recompiles per test method, making the full IDE suite slow. | [tinox#514](https://github.com/subnix-work/tinox/issues/514), closed upstream | Use the documented per-file runner; benchmark the replacement binary before retiring it. A closed issue alone does not update the installed compiler. |
| Cross-file definitions and zero-width symbol ranges limit navigation/breadcrumb precision. | [tinox#578](https://github.com/subnix-work/tinox/issues/578), [#577](https://github.com/subnix-work/tinox/issues/577), open | Exact-name workspace-symbol fallback keeps ambiguity visible; conservative ranges support breadcrumbs. [Navigation](navigation.md) documents the limits and real regression smoke. |
| `contains(Position)` and Bool-list comparisons generate invalid LLVM IR in the installed compiler. | [tinox#576](https://github.com/subnix-work/tinox/issues/576), [#580](https://github.com/subnix-work/tinox/issues/580), open | Explicitly named range predicate and integer passes, with referenced reproductions. Retest the fixed compiler before simplifying. |
| LSP request/cancel churn can saturate runtime Map tombstones and hang missing-key lookups. | [tinox#581](https://github.com/subnix-work/tinox/issues/581), open | Rebuild pending maps; churn regression preserves a surviving request. Remove only after the runtime passes that regression. |
| Installable builds cannot rely on unpublished `Fs.realPath`; runtime.c lookup is tied to system/development paths. | [tinox#584](https://github.com/subnix-work/tinox/issues/584), [#583](https://github.com/subnix-work/tinox/issues/583), open | [Packaging](packaging.md) pins public sources, uses checked coreutils canonicalization and retains the build-only compiler tree until linking. Move to a compatible relocatable release when available. |
| Ripgrep EOF could arrive before exit status and hide invalid-regex errors. | [IDE#60](https://github.com/subnix-work/tinox-ide/issues/60), closed | Fixed status wait; delayed-exit regression and full search tests protect the result. See [testing](testing.md). |

Toolchain for this checkpoint: `tinox --version` reports 2.8.0; the local checkout
is `753082565c95a1367c17dd4a5f2cbbf95d465925`. The separately pinned public Flatpak
compiler revision is recorded in [packaging](packaging.md). Neither version label
is evidence that unpublished fixes are available in the other build.

## Milestone review record

| Milestone | Review at this checkpoint | Evidence / remaining gate |
|---|---|---|
| M0 / M1 foundations | Retrospective documentation review, 2026-10-29 | [ADR and spike](adr/0001-spike-findings.md), ABI ownership rules and bridge tests are linked. This is not a backdated historical sign-off. |
| M2 language intelligence | Current compatibility review, 2026-10-29 | Real diagnostics/navigation/editing/semantic smokes in `make test`; carry the open LSP/compiler rows above. |
| M3 build/run/test | Current functional review, 2026-10-29 | `make test-dogfood` rebuilds/runs the IDE and runs a selected test file; full `make test` remains the commit gate. Carry discovery/filtering/performance constraints. |
| M4 polish/documentation | Documentation/self-hosting checkpoint, 2026-10-29 | `make docs` checks generated paths/anchors; tutorial build/run/test is executable. This is **not release approval**: accessibility review #32 and the broader CI work #34 remain open. |

The #35 documentation was also opened, changed and saved through real KTextEditor
in the IDE with isolated settings using a one-off authoring harness. The retained
self-hosting smoke reproduces source editing/build/run/test in a disposable copy.
No manual screen-reader or display-backend review is claimed by either harness.
