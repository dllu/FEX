#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

static volatile LONG stop_worker;
extern char suspend_loop __asm__("suspend_loop"), suspend_end __asm__("suspend_end");

/* No calls or unconditional back-edge: suspension must be serviced by JE. */
__attribute__((naked,noinline)) static DWORD WINAPI worker(void *unused)
{
#ifdef __x86_64__
    __asm__ volatile(
        "mov %rcx,%rdx\n"
        "mov $1,%eax\n"
        ".global suspend_loop\n"
        "suspend_loop: mov %eax,%ecx\n"
        "xor $0x12345,%ecx\n"
        "inc %eax\n"
        "cmpl $0,(%rdx)\n"
        "je suspend_loop\n"
        ".global suspend_end\n"
        "suspend_end: xor %eax,%eax\n"
        "ret\n");
#else
    __asm__ volatile(
        "mov 4(%esp),%edx\n"
        "mov $1,%eax\n"
        ".global suspend_loop\n"
        "suspend_loop: mov %eax,%ecx\n"
        "xor $0x12345,%ecx\n"
        "inc %eax\n"
        "cmpl $0,(%edx)\n"
        "je suspend_loop\n"
        ".global suspend_end\n"
        "suspend_end: xor %eax,%eax\n"
        "ret $4\n");
#endif
}

int main(void)
{
    HANDLE thread = CreateThread(NULL, 0, worker, (void *)&stop_worker, 0, NULL);
    unsigned checked = 0;
    int bad = 0;
    if (!thread) return 2;
    Sleep(10);
    for (unsigned i = 0; i < 500; i++) {
        CONTEXT context = {0};
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (SuspendThread(thread) == (DWORD)-1) return 3;
        BOOL ok = GetThreadContext(thread, &context);
        DWORD previous = ResumeThread(thread);
        if (!ok || previous == (DWORD)-1 || previous == 0) return 4;
#ifdef __x86_64__
        uintptr_t pc = context.Rip;
        uint32_t counter = context.Rax, relation = context.Rcx;
#else
        uintptr_t pc = context.Eip;
        uint32_t counter = context.Eax, relation = context.Ecx;
#endif
        if (pc >= (uintptr_t)&suspend_loop && pc < (uintptr_t)&suspend_end) {
            checked++;
            if (relation != ((counter - 1) ^ 0x12345)) {
                fprintf(stderr, "invalid suspended relation at %p: %x %x\n",
                        (void *)pc, counter, relation);
                bad = 1;
                break;
            }
        }
    }
    InterlockedExchange(&stop_worker, 1);
    if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0) bad = 1;
    CloseHandle(thread);
    if (checked < 400) bad = 1;
    printf("%u-bit conditional-backedge suspension/context: %s (%u checked)\n",
           (unsigned)(8 * sizeof(void *)), bad ? "FAIL" : "PASS", checked);
    return bad;
}
