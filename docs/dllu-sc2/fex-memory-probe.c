#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <malloc.h>

struct publication { volatile uint32_t flag, ack; volatile uint64_t first, second; } shared;
static volatile LONG failed;

static DWORD WINAPI consume(void *unused)
{
    (void)unused;
    for (uint32_t i = 1; i <= 100000; ++i)
    {
        while (shared.flag != i) YieldProcessor();
        if (shared.first != i || shared.second != ~((uint64_t)i)) InterlockedExchange(&failed, 1);
        shared.ack = i;
    }
    return 0;
}

int main(void)
{
    unsigned char *storage = _aligned_malloc(32768, 4096);
    unsigned char *base;
    uintptr_t value = (uintptr_t)0x87654321fedcba98ull, result;
    HANDLE thread;
    if (!storage) return 2;
    base = storage + 8192;
#define ONE(offset, suffix, operand, mask) do { \
    __asm__ volatile("mov" suffix " %" operand "2, " #offset "(%1)\n\t" \
                     "mov" suffix " " #offset "(%1), %" operand "0" \
                     : "=r"(result) : "r"(base), "r"(value) : "memory"); \
    if ((result & (mask)) != (value & (mask))) { printf("bad " suffix " at %d\n", offset); failed = 1; } \
} while (0)
#define OFFSETS(suffix, operand, mask) do { \
    ONE(-256, suffix, operand, mask); ONE(-255, suffix, operand, mask); \
    ONE(-129, suffix, operand, mask); ONE(-128, suffix, operand, mask); \
    ONE(-1, suffix, operand, mask); ONE(0, suffix, operand, mask); \
    ONE(1, suffix, operand, mask); ONE(7, suffix, operand, mask); \
    ONE(8, suffix, operand, mask); ONE(15, suffix, operand, mask); \
    ONE(16, suffix, operand, mask); ONE(127, suffix, operand, mask); \
    ONE(128, suffix, operand, mask); ONE(255, suffix, operand, mask); \
    ONE(256, suffix, operand, mask); ONE(4093, suffix, operand, mask); \
} while (0)
    for (int i = 0; i < 10000; ++i)
    {
        OFFSETS("b", "b", 0xffull);
        OFFSETS("w", "w", 0xffffull);
        OFFSETS("l", "k", 0xffffffffull);
#ifdef __x86_64__
        OFFSETS("q", "", UINT64_MAX);
#endif
        value += 0x102030405060708ull;
    }
    thread = CreateThread(NULL, 0, consume, NULL, 0, NULL);
    if (!thread) return 3;
    for (uint32_t i = 1; i <= 100000; ++i)
    {
        while (shared.ack != i - 1) YieldProcessor();
        shared.first = i;
        shared.second = ~((uint64_t)i);
        shared.flag = i;
    }
    if (WaitForSingleObject(thread, 20000) != WAIT_OBJECT_0) return 4;
    CloseHandle(thread);
    _aligned_free(storage);
    printf("aligned/unaligned signed-offset accesses and publication: %s\n", failed ? "FAIL" : "PASS");
    return failed;
}
