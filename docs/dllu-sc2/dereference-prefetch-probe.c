#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORK "xor %r8d,%r8d\n.rept 8\nlea 1(%r8),%r8\n.endr\n"
__attribute__((naked,noinline)) uint64_t selected(uint64_t **slot)
{
    __asm__ volatile("mov (%rcx),%rax\n" WORK
                     ".globl selected_fault_site\nselected_fault_site:\n"
                     "mov 72(%rax),%rax\nret\n");
}
__attribute__((naked,noinline)) uint64_t selected_negative(uint64_t **slot)
{
    __asm__ volatile("mov (%rcx),%rax\n" WORK "mov -72(%rax),%rax\nret\n");
}
__attribute__((naked,noinline)) uint64_t selected_odd(uint64_t **slot)
{
    __asm__ volatile("mov (%rcx),%rax\n" WORK "mov 73(%rax),%rax\nret\n");
}
__attribute__((naked,noinline)) uint64_t selected_jump(uint64_t **slot)
{
    __asm__ volatile("mov (%rcx),%rax\n" WORK "jmp 1f\nud2\n1: mov 72(%rax),%rax\nret\n");
}
__attribute__((naked,noinline)) uint64_t immediate(uint64_t **slot)
{
    __asm__ volatile("mov (%rcx),%rax\nmov 72(%rax),%rax\nret\n");
}
__attribute__((naked,noinline)) uint64_t overwritten(uint64_t **slot, uint64_t *other)
{
    __asm__ volatile("mov (%rcx),%rax\nmov %rdx,%rax\n" WORK "mov 72(%rax),%rax\nret\n");
}
__attribute__((naked,noinline)) uint64_t skipped(uint64_t **slot, uint64_t skip)
{
    __asm__ volatile("mov (%rcx),%rax\ntest %edx,%edx\njnz 1f\n" WORK
                     "mov 72(%rax),%rax\nret\n1: mov $123,%eax\nret\n");
}
extern char selected_fault_site;
static unsigned expected_faults, faults;
static LONG CALLBACK handler(EXCEPTION_POINTERS *p)
{
    if (expected_faults && p->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        p->ContextRecord->Rip == (uintptr_t)&selected_fault_site &&
        p->ExceptionRecord->ExceptionInformation[1] == 72) {
        ++faults;
        p->ContextRecord->Rip += 4;
        p->ContextRecord->Rax = 123;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
int main(void)
{
    uint64_t values[32] = {0}, *slot, odd_expected;
    unsigned failures = 0;
    AddVectoredExceptionHandler(1, handler);
    for (unsigned i = 0; i < 10000; ++i) {
        values[9] = i * 6364136223846793005ULL;
        values[0] = ~values[9];
        slot = values;
        failures += selected(&slot) != values[9];
        failures += selected_jump(&slot) != values[9];
        failures += immediate(&slot) != values[9];
        memcpy(&odd_expected, (char *)values + 73, sizeof(odd_expected));
        failures += selected_odd(&slot) != odd_expected;
        slot = values + 9;
        failures += selected_negative(&slot) != values[0];
        slot = (void *)(uintptr_t)-1;
        failures += overwritten(&slot, values) != values[9];
        failures += skipped(&slot, 1) != 123;
        slot = NULL;
        failures += skipped(&slot, 1) != 123;
        expected_faults = 1;
        failures += selected(&slot) != 123;
        expected_faults = 0;
    }
    failures += faults != 10000;
    /* Fill the buffered perf-map writer so the preceding probe entries are
       available to the read-only checker while this process is held. */
    unsigned char *code = VirtualAlloc(NULL, 128 * 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!code) return 2;
    for (unsigned i = 0; i < 128; ++i) {
        unsigned char *fn = code + i * 16;
        fn[0] = 0xb8;
        memcpy(fn + 1, &i, sizeof(i));
        fn[5] = 0xc3;
    }
    FlushInstructionCache(GetCurrentProcess(), code, 128 * 16);
    for (unsigned i = 0; i < 128; ++i)
        failures += ((unsigned (*)(void))(code + i * 16))() != i;

    printf("loaded-pointer offsets, immediate/clobbered controls, skipped invalid pointers, precise null faults: %s (%u failures, %u handled faults)\n",
           failures ? "FAIL" : "PASS", failures, faults);
    printf("selected=%p negative=%p odd=%p jump=%p immediate=%p overwritten=%p skipped=%p\n",
           selected, selected_negative, selected_odd, selected_jump, immediate, overwritten, skipped);
    fflush(stdout);
    if (getenv("HOLD_PROBE_SECONDS")) Sleep(atoi(getenv("HOLD_PROBE_SECONDS")) * 1000);
    return failures != 0;
}
