#include <windows.h>
#include <stdio.h>
#include <stdint.h>

__attribute__((naked,noinline)) uint64_t selected(volatile uint64_t *p)
{
    __asm__("movq (%rcx),%rax\n"
            "movq %rax,8(%rcx)\n"
            "testq %rax,%rax\n"
            "jz 1f\n"
            "movq 16(%rcx),%rax\n"
            "movq %rax,24(%rcx)\n"
            "1: retq\n");
}
__attribute__((naked,noinline)) uint64_t outside(volatile uint64_t *p)
{
    __asm__("movq (%rcx),%rax\n"
            "movq %rax,8(%rcx)\n"
            "retq\n");
}
int main(void)
{
    volatile uint64_t data[4] = {1,0,2,0};
    selected(data);
    outside(data);
    printf("selected=%p outside=%p values=%llu,%llu\n", selected, outside,
           (unsigned long long)data[1], (unsigned long long)data[3]);
    fflush(stdout);
    Sleep(300000);
    return 0;
}
