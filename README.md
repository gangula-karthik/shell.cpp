[![progress-banner](https://backend.codecrafters.io/progress/shell/d36dbd6c-55f1-4454-b28e-845ec072401f)](https://app.codecrafters.io/users/gangula-karthik?r=2qF)

# Shell (C++)

A POSIX-compliant shell implementation written in C++.

## Features

- Interactive REPL with a custom raw-mode terminal reader
- Command history with up/down arrow navigation
- Tab completion for builtin commands and executables in `PATH`
- Single-quote, double-quote, and backslash escape handling
- Builtin commands: `exit`, `echo`, `type`, `pwd`, `cd`, `jobs`, `history`
- Execution of external programs via `fork`/`exec`
- Background job execution with `&`
- Job listing and status tracking
- Persistent command history (loads/saves from `HISTFILE`)
- `PATH` and `HOME` environment resolution
- C++23 standard

## Building

Ensure you have `cmake` and `vcpkg` installed, with `VCPKG_ROOT` set.

```sh
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
cmake --build ./build
```

## Running

```sh
./run.sh
```

Or run the compiled binary directly:

```sh
./build/shell
```
