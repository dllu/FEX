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
access backpatching remain enabled. The 32-bit Windows frontend retains its previous register-only TSO addressing
on affected CPUs. The new host-feature bit is included in
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

## Optional populated-heap promotion

`FEX_HEAP_HUGEPAGES=SC2_x64.exe` enables a native Unix worker only when the
process basename matches that value. `1` enables it for every process using
the helper; an unset value or `0` leaves it off. Use it with the matching
Wine's `WINE_HUGEPAGES=1` allocation policy. It reads `smaps` every two seconds
and advises at most 64 MiB per pass with `MADV_COLLAPSE`. Only unnamed,
anonymous private writable VMAs already marked `hg`, at least 80% resident,
and less than 75% huge-backed qualify. Explicit `nh` ranges, named FEX
lookup/metadata ranges, executable mappings and sparse reservations are
excluded. Work rotates through the eligible ranges rather than repeating
only the start of a large VMA.

The worker blocks Wine-handled signals before thread creation, restores the
parent mask, and never calls Windows APIs. Its lifetime is the process's;
the Unix helper remains loaded. The launcher scopes the CPU placement of this
thread together with SC2's other workers.

Build `heap-collapse-probe.c` natively with `cc -O2 -ldl` and pass the helper
`.so` path. It checks promotion of a populated 96 MiB range across successive
bounded passes, unchanged data, an explicit no-huge-page range, 4 KiB
protection changes and the parent's signal mask. It passes on the GB10.
The feature remains opt-in while replay measurements continue.

## Volatile range regression

`Core.cpp` must mark a block as covered by volatile metadata independently of
whether that block contains an explicit volatile instruction. Previously, an
empty instruction list, or an ordered instruction in a different block, made
the declared range ineffective. The local fix moves range membership outside
the instruction lookup. It does not change code without volatile metadata.

`volatile-codegen-probe.c` and `check-volatile-codegen.py` check actual generated
ARM instructions, rather than only parser output. Build the C source with the
LLVM-MinGW 20261006 x64 compiler and `-O0`. Verify `selected` is at RVA `1470`
and `outside` at `1490` with `llvm-nm` (the checker deliberately fails if these
addresses change). Run in a disposable, matching Wine/FEX prefix with:

```
FEX_MAXINST=1 FEX_MULTIBLOCK=0 FEX_BLOCKJITNAMING=1 \
FEX_EXTENDEDVOLATILEMETADATA='volatile-codegen-probe.exe;1470-1488;147c'
```

Then run the read-only checker with `sudo python3 check-volatile-codegen.py
--pid <Linux PID> --expect mixed`. The first load and stores in the selected
range must be ordinary accesses; the explicitly ordered load at `147c` and
both outside accesses must remain ordered. This fails with the prior runtime
and passes with the fix. Repeat with the metadata variable unset and
`--expect ordered`, then with its value `volatile-codegen-probe.exe` and
`--expect relaxed`; both pass. x64/i386 `fex-memory-probe.c` also pass with
normal ordering enabled. Relaxed whole-application replay runs are diagnostic
only and are never a production configuration or evidence of achieving the
correctness-preserving FPS target.

## Exception-context reserved flags

`CPUState.flags[24..27]` stores packed Arm NZCV, rather than architectural
x86 flag bytes. `ReconstructCompactedEFLAGS` must skip these bytes in its
initial flag-compaction loop. In particular, the packed overflow byte could
leak into reserved EFLAGS bit31 in WoW64 exception contexts. The subsequent
condition-flag reconstruction already handles OF/CF/ZF/SF separately.

`exception-flags-probe.c`, built with LLVM-MinGW `-O2` for i386 and x64,
alternates overflowing ADD and XOR followed by handled privileged-instruction
faults. It checks reserved bits and OF in the actual vectored-handler context.
Before the fix, i386 reports `80000a96` / `80000256` and fails; afterward it
reports `00000a96` / `00000256` and passes. The x64 control passes before and
after. x64/i386 memory/publication and i386 event/semaphore/mutex/APC probes
also pass. This is a correctness fix; SC2's 32-bit startup and performance
remain separate validation steps.

## Conditional loop suspension

Conditional branches to an already-bound backwards target now check pending
cooperative interrupts, as unconditional back-edges already do. Previously a
tight conditional loop could bypass every check after its initial entry.
SuspendThread then hung with a nonzero suspend doorbell while the worker kept
running. A read-only native debugger check confirmed the pending doorbell and
the loop's missing check. This reproduced with register coalescing disabled.

The x64 `register-cache-probe.c` checks 10,000 arithmetic/fault-context cases,
then repeatedly suspends and reads a worker executing a conditional backwards
loop. Before the branch fix the suspension phase hangs; afterward all 500
suspended register relations pass with ordinary per-instruction flushing.
The fault-context arithmetic and zero-extension checks also pass. Build with
LLVM-MinGW's x64 compiler and `-O2`. The probe does not patch a game binary.

The smaller `conditional-suspend-probe.c` supports both i386 and x64. Its
worker has no calls or unconditional loop branch; it preserves a counter
relation across the conditional back-edge. Both builds pass 500 suspended
contexts with the fixed runtime, alongside the existing memory/publication,
exception-flags and event/semaphore/mutex/APC probes. Compile each with the
corresponding LLVM-MinGW compiler and `-O2`.

## Experimental nearby guest addresses

This branch adds default-off `ReuseGuestAddresses` (`FEX_REUSEGUESTADDRESSES=1`)
for conservative straight-line sequences of entrypoint-relative addresses and
memory loads. It replaces repeated constant materialization with ADD/SUB when
a known address remains in a GPR. Loads invalidate their GPR destination; all
other operations and block boundaries clear the cache. Only 64-bit addresses
participate. Derived addresses retain the first address's module relocation
delta. The option participates in the code-cache configuration hash.

`guest-address-probe.c`, built for x64 with LLVM-MinGW `-O2`, checks 10,000 sets
of RIP-relative loads/LEAs, positive and negative deltas, larger offsets,
clobbers and branches. Both modes pass. The load-chain mapped compilation
span shrinks from 272 to 240 bytes, and the LEA chain from 240 to 208 bytes;
these extents include metadata. Actual native dumps show the new ADD/SUB
instructions. x64/i386 memory/publication, exception flags, synchronization
and suspension probes also pass with the feature enabled.

This establishes exercised correctness and activation, not a game speedup.
The first latest-replay heavy pass is 39.54 FPS versus the control's 39.74;
a warmed comparison is pending. The production branch retains only the
conditional-backedge suspension fix. No quality or memory ordering setting
is relaxed, and the canonical launcher does not enable this experiment.
