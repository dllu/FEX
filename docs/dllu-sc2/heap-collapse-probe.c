#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

static size_t huge_kb(void *pointer)
{
    FILE *f = fopen("/proc/self/smaps", "re");
    char line[2048];
    unsigned long low, high, huge = 0;
    int selected = 0;
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%lx-%lx", &low, &high) == 2) selected = (uintptr_t)pointer >= low && (uintptr_t)pointer < high;
        else if (selected && sscanf(line, "AnonHugePages: %lu", &huge) == 1) break;
    }
    fclose(f);
    return huge;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    size_t size = 96u * 1024 * 1024;
    unsigned char *eligible = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned char *disabled = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (eligible == MAP_FAILED || disabled == MAP_FAILED) return 3;
    for (size_t i = 0; i < size; i += 4096) eligible[i] = disabled[i] = (i / 4096) % 251;
    madvise(eligible, size, MADV_HUGEPAGE);
    madvise(disabled, size, MADV_NOHUGEPAGE);
    const char *base = strrchr(argv[0], '/');
    setenv("FEX_HEAP_HUGEPAGES", base ? base + 1 : argv[0], 1);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { puts(dlerror()); return 4; }
    typedef int32_t (*handler_fn)(void *);
    const handler_fn *functions = dlsym(library, "__wine_unix_call_funcs");
    uint32_t pid = 0;
    sigset_t before, after;
    sigprocmask(SIG_SETMASK, NULL, &before);
    if (!functions || functions[7](&pid) || pid != (uint32_t)getpid()) return 5;
    sigprocmask(SIG_SETMASK, NULL, &after);
    for (int i = 1; i < NSIG; ++i) if (sigismember(&before, i) != sigismember(&after, i)) return 6;
    printf("initial eligible=%zu KiB disabled=%zu KiB\n", huge_kb(eligible), huge_kb(disabled));
    fflush(stdout);
    size_t promoted = 0;
    for (int i = 0; i < 16; ++i) {
        sleep(1);
        promoted = huge_kb(eligible);
        if (promoted >= 90u * 1024) break;
    }
    if (promoted < 90u * 1024 || huge_kb(disabled)) return 7;
    for (size_t i = 0; i < size; i += 4096)
        if (eligible[i] != (i / 4096) % 251 || disabled[i] != (i / 4096) % 251) return 8;
    if (mprotect(eligible + 4 * 1024 * 1024, 4096, PROT_NONE)) return 9;
    if (mprotect(eligible + 4 * 1024 * 1024, 4096, PROT_READ | PROT_WRITE)) return 10;
    if (eligible[4 * 1024 * 1024] != (4 * 1024 * 1024 / 4096) % 251) return 11;
    printf("promoted=%zu KiB; no-huge override, content and 4K protection preserved: PASS\n", promoted);
    // The detached helper belongs to the process; leave its DSO loaded until exit.
    return 0;
}
