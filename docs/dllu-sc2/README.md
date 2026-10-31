# Personal SC2 runtime notes

This branch is for dllu's local GB10 experiments, not upstream submission.
The matching Wine branch is `dllu/sc2-performance` in `dllu/wine`.

## Native Unix helper

The Windows DLL pair alone is insufficient for Linux huge-page advice, VMA
names and Linux process IDs. Build the helper from the same checkout and
install it beside the DLLs in a directory on `WINEDLLPATH`:

```sh
cmake -S Source/Windows/UnixLib -B build-unixlib \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$FEX_INSTALL"
cmake --build build-unixlib -j4
cmake --install build-unixlib
```

Keep `libarm64ecfex.dll`, `libwow64fex.dll`, `libarm64ecfex.so` and
`libwow64fex.so` together. Wine searches each `WINEDLLPATH` directory for the
native `.so` helpers. Inspect `/proc/<Linux game PID>/maps` to verify loading.
With the host THP policy set to `madvise`, code buffers should gain `hg` in
`VmFlags` and nonzero `AnonHugePages` in `smaps`. Sparse lookup caches keep their
explicit `MADV_NOHUGEPAGE` policy.

## LDAPUR workaround and STLUR

Cortex-X925 is covered by the existing LDAPUR ordering workaround. The local
change retains `LDAPR` for loads while permitting `STLUR`'s signed immediate
addressing for release stores. An immediate-offset load forms its address in a
reserved temporary register before `LDAPR`. Memory ordering and unaligned
access backpatching remain enabled. The new host-feature bit is included in
the existing code-cache hash. Explicit `enablrcpc2` overrides retain their old
meaning.

`fex-memory-probe.c` checks positive/negative immediate offsets, aligned and
unaligned 8/16/32/64-bit accesses, cache-line/page crossings and 100,000 rounds
of two-thread publication. Compile with LLVM-MinGW's
`x86_64-w64-mingw32-clang -O2` and `i686-w64-mingw32-clang -O2`; both executables
pass under the candidate DLLs with matching Wine 11.21. The i386 test requires
Wine's i386 Windows DLLs in the test prefix's `syswow64` directory.

These checks establish correctness for the exercised cases. Heavy-battle
performance remains an ongoing replay benchmark, at native Wayland and
3840x2160 maximum graphics/physics; they do not establish a 60 FPS minimum.
