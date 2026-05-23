# Contributing

## Getting started

1. Fork → clone → create a branch from `master`.
2. Build with CMake (see README).
3. Run clang-format before committing:
   ```bash
   find . \( -name '*.cpp' -o -name '*.c' -o -name '*.h' \) ! -path './build/*' \
     -exec clang-format -i {} +
   ```
4. Open a PR with a clear description of the change and why.

## Coding conventions

- C++14, no exceptions, no RTTI.
- Tabs for indentation (see `.clang-format`).
- Keep `main.cpp` / `xbit.h` changes minimal and backward-compatible where possible.
- Prefer descriptive variable names over comments.

## Reporting issues

Include:
- OS + distro / version
- USB controller (`lspci | grep USB` on Linux)
- `dmesg` output around the time you plugged the chip in
- Exact command you ran and its full output

## Testing

There are no automated tests (hardware required). If you have the hardware, please
test read → write → verify on at least one bank before submitting a flash-related PR.
