#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

volatile uint64_t address_slots[1024];

__attribute__((naked,noinline)) static uint64_t load_chain(void)
{
    __asm__ volatile(
        "mov address_slots+24(%rip),%r8\n"
        "mov address_slots+16(%rip),%r9\n"
        "mov address_slots+8(%rip),%rax\n"
        "mov address_slots(%rip),%rdx\n"
        "xor %r8,%rax\n"
        "add %r9,%rax\n"
        "xor %rdx,%rax\n"
        "mov address_slots+8184(%rip),%r10\n"
        "xor %r10,%rax\n"
        "ret\n");
}

__attribute__((naked,noinline)) static uint64_t address_chain(void)
{
    __asm__ volatile(
        "lea address_slots+16(%rip),%rax\n"
        "lea address_slots+8(%rip),%rdx\n"
        "lea address_slots(%rip),%r8\n"
        "lea address_slots+4096(%rip),%r9\n"
        "lea address_slots+8184(%rip),%r10\n"
        "xor %rdx,%rax\n"
        "add %r8,%rax\n"
        "xor %r9,%rax\n"
        "add %r10,%rax\n"
        "ret\n");
}

/* Different incoming edges and loaded data must invalidate address knowledge. */
__attribute__((naked,noinline)) static uint64_t branch_chain(uint64_t selector)
{
    __asm__ volatile(
        "lea address_slots(%rip),%rax\n"
        "test %ecx,%ecx\n"
        "jz 1f\n"
        "mov address_slots+8(%rip),%rax\n"
        "lea address_slots+16(%rip),%rdx\n"
        "xor %rdx,%rax\n"
        "ret\n"
        "1: mov address_slots+16(%rip),%rax\n"
        "lea address_slots+8(%rip),%rdx\n"
        "add %rdx,%rax\n"
        "ret\n");
}

int main(void)
{
    uint64_t seed = 0x123456789abcdef0ULL;
    unsigned failures = 0;
    uint64_t addresses = ((((uintptr_t)&address_slots[2] ^ (uintptr_t)&address_slots[1]) +
                            (uintptr_t)&address_slots[0]) ^ (uintptr_t)&address_slots[512]) +
                            (uintptr_t)&address_slots[1023];
    for (unsigned i = 0; i < 10000; i++) {
        for (unsigned j = 0; j < 4; j++) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            address_slots[j] = seed;
        }
        address_slots[1023] = ~seed;
        uint64_t expected = (((address_slots[1] ^ address_slots[3]) + address_slots[2]) ^
                              address_slots[0]) ^ address_slots[1023];
        if (load_chain() != expected || address_chain() != addresses ||
            branch_chain(1) != (address_slots[1] ^ (uintptr_t)&address_slots[2]) ||
            branch_chain(0) != (address_slots[2] + (uintptr_t)&address_slots[1])) failures++;
    }
    printf("nearby RIP-relative loads/LEAs, clobbers and branch joins: %s (%u failures)\n",
           failures ? "FAIL" : "PASS", failures);
    fflush(stdout);
    if (getenv("HOLD_PROBE_SECONDS")) Sleep(atoi(getenv("HOLD_PROBE_SECONDS")) * 1000);
    return failures != 0;
}
