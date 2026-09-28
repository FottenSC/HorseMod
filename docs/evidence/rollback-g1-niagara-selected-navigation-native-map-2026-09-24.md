# G1 selected callback navigation targets

Read-only Ghidra MCP inspection of the existing `SoulcaliburVI.exe` program, image base `140000000`, game SHA-256 `f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553`. Queries explicitly selected this program. No database edit or game run was made. This follows the three navigation-library calls in the retained [selected Kismet reference audit](rollback-g1-niagara-selected-script-reference-audit-2026-09-24.json); it does not close the selected application's producer graph.

`140CBBD10` registers `LuxUINavigationWindowFunctions`; its registrar `140CBD130` adds 13 native functions from table `1434905A0`. The exact relevant entries and native calls are:

| Reflected call | Table entry | Native thunk | Verified call chain |
| --- | --- | --- | --- |
| `HideNaviWindow` | `1434905F0` | `140CF1F80` | `140645590` -> `142F34420` |
| `SetNaviText` | `143490640` | `140CF38A0` | `140CBCF10` -> `14064F390` |
| `ShowNaviWindow` | `143490650` | `140CF3EA0` | `140650A60` -> `142F4EF50` or `142F49330` |

The hide/show wrappers resolve a weak UI object. The hide target `142F34420` manipulates data-table keys `startfadeout`, `windowShown`, `allowWindowActivate`, `cache_enable` and `finishfadeout`; it makes several virtual calls and installs or executes fade/window operations. `14064F390`, reached by `SetNaviText`, writes navigation-window text and may call `140630110`. The show target also enters deeper UI operations. These are native side-effect routes, not demonstrated leaf functions. The existing script audit's lack of direct Niagara names therefore cannot be promoted to an exclusion for this callback, much less all corrected application callbacks.

The immediate script parent of `ReplayBattleScene.OnUpdateMatch` is `BattleScene.PollingFinishedMatch` at Kismet offset 17890. Historical parsed Kismet evidence records timer rearming in `PollingFinishedMatch` and an `OnRestart` call to it; [prior investigation](../history/2026-09-28-consolidation/investigations/replay-seeker-static-deepening-2026-09-08.md) has the exact branches. Neither this static parent relation nor the navigation target map establishes all dynamic or reflected calls for the selected application. A finite pre-application producer census with invalidation, or a concrete native owner transfer for one callback, remains necessary before a new admission RED/GREEN.
