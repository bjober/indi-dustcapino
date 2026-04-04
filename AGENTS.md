# AGENTS.md

## Cursor Cloud specific instructions

### Project overview

DustCapIno is an INDI driver (C++17) for an Arduino-based telescope dust cap controller. The repository has two components:

- **`driver/`** — C++ INDI driver built with CMake, links against `libindi` (the `indidriver` library)
- **`firmware/`** — Arduino sketch (`dustcapino.ino`) for the microcontroller (cannot be built/tested without Arduino tooling)

### System dependencies

The INDI PPA (`ppa:mutlaqja/ppa`) must be enabled to get INDI 2.x headers and libraries. The Ubuntu default repo only has INDI 1.9.x which is API-incompatible with this driver. Required packages: `libindi-dev`, `indi-bin`.

The default C++ compiler (Clang 18) may fail to link due to missing `libstdc++` in its search path. Use GCC explicitly when configuring CMake:

```
cmake -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc ..
```

### Build

```
cd driver && mkdir -p build && cd build
cmake -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc ..
make
```

### Run

```
cd driver/build
indiserver -v ./indi_dustcapino
```

The server listens on port 7624. Query properties with `indi_getprop -t 3`.

Since no physical Arduino hardware is available in the cloud VM, the driver will start and expose all INDI properties but cannot complete a hardware connection (auto-detect will fail). This is expected behavior for a development/build environment.

### Lint / static analysis

No linter or static analysis tooling is configured in the repository. Compilation warnings from `g++` serve as the primary code quality check.

### Tests

No automated test suite exists in this repository. Validation is done by building the driver and running it with `indiserver`.
