# Simulators

Each subfolder is an independent WRENCH/SimGrid simulator implementing one scheduling heuristic.

| Folder | Heuristic |
|--------|-----------|
| `CarbonPolywrench_HEFT/`  | HEFT  |
| `CarbonPolywrench_MOHEFT/`| MOHEFT |
| `CarbonPolywrench_PEFT/`  | PEFT  |
| `CarbonPolywrench_IPEFT/` | IPEFT |

## Folder layout (same for all)

```
CarbonPolywrench_<HEURISTIC>/
├── src/
│   ├── Simulator.cpp          # Entry point
│   ├── Controller.cpp         # Scheduling logic
│   └── host_carbon_footprint.cpp
├── include/
│   ├── Controller.h
│   ├── host_carbon_footprint.h
│   └── json.hpp               # nlohmann/json (header-only)
├── data/
│   └── platform.xml           # SimGrid platform definition
├── CMakeModules/
│   ├── FindWRENCH.cmake
│   ├── FindSimGrid.cmake
│   └── FindFSMod.cmake
└── CMakeLists.txt
```

## Build

```bash
cd CarbonPolywrench_HEFT   # or MOHEFT / PEFT / IPEFT
mkdir build && cd build
cmake ..
make
```

The binary `my-wrench-simulator` is created inside `build/`.

> **Note:** The `build/` directory is git-ignored. Always rebuild after cloning.
