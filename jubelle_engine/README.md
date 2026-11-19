# Jubelle engine (development area)

This folder holds the **next-generation Jubelle notation engine**. It is developed on the branch `engine/next`.
The central reference is **[`JUBELLE_ENGINE_DEVELOPMENT_MASTER_PLAN.md`](../JUBELLE_ENGINE_DEVELOPMENT_MASTER_PLAN.md)**
at the repository root.

## Rules of this area
1. **Nothing here is built into the released Jubelle** until a milestone is explicitly integrated behind the
   experimental switch (off by default). The release on `main` and the stable checkpoint
   (`checkpoint/stable-4.7.7.261118230`, tag `v4.7.7.261118230`) are never changed by engine work.
2. The engine core is plain C++17 with **no Qt** (only `bridge/` may use Qt).
3. Every engine component is checked against MuseScore's engine in **shadow mode** before it is used for real.
4. Automatic condensing is **specified only** (`docs/04_condensing_specification.md`). It is not implemented until
   the user approves Phase 6.
5. Ported MuseScore code keeps its licence header (`docs/05_licensing.md`).

## Layout
| Folder | Purpose |
|---|---|
| `docs/` | audit, architecture, condensing specification, licensing, roadmap and testing, decisions log |
| `model/` `edit/` `present/` `layout/` `render/` `perform/` `io/` `style/` `exec/` | engine modules (see each README) |
| `bridge/` | Qt-side integration with the Jubelle application |
| `tests/` `bench/` | tests and benchmarks |

## Documents
1. [Architecture audit](docs/01_architecture_audit.md): MuseScore 4.7.7 as Jubelle uses it
2. [Jubelle integration map](docs/02_jubelle_integration_map.md): every Jubelle feature, its hooks and its route
3. [Engine architecture](docs/03_engine_architecture.md): layers, modules, data model, pipeline, integration
4. [Condensing specification](docs/04_condensing_specification.md)
5. [Licensing](docs/05_licensing.md)
6. [Roadmap and testing](docs/06_roadmap_and_testing.md)
7. [Decisions log](docs/07_decisions_log.md)
