#include <windows.h>
#include <stdio.h>
static volatile LONG count,bad;
static DWORD seen[64];
static LONG CALLBACK handler(EXCEPTION_POINTERS *e) {
 if(e->ExceptionRecord->ExceptionCode!=EXCEPTION_PRIV_INSTRUCTION)return EXCEPTION_CONTINUE_SEARCH;
 DWORD f=e->ContextRecord->EFlags;LONG i=count++;
 if(i<64)seen[i]=f;
 if((f&0xffc00000)||((f&0x800)!=((i&1)?0:0x800)))bad=1;
#ifdef _WIN64
 e->ContextRecord->Rip++;
#else
 e->ContextRecord->Eip++;
#endif
 return EXCEPTION_CONTINUE_EXECUTION;
}
int main(void) {
 if(!AddVectoredExceptionHandler(1,handler))return 2;
 for(int i=0;i<16;++i) __asm__ volatile("movl $0x7fffffff,%%eax\n\taddl $1,%%eax\n\thlt\n\txorl %%eax,%%eax\n\thlt":::"eax","cc","memory");
 for(int i=0;i<count&&i<8;++i)printf("exception%d flags=%08lx\n",i,seen[i]);
 printf("reserved context flags and overflow preservation: %s (%ld exceptions)\n",!bad&&count==32?"PASS":"FAIL",count);
 return bad||count!=32;
}
