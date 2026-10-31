#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
static volatile LONG faults,bad;
static uint64_t want_a,want_b,want_c;
extern char chain_fault;
__attribute__((naked,noinline)) static uint64_t chain(uint64_t seed)
{
 __asm__ volatile(
 "mov %rcx,%rax\n"
 "mov %rax,%r8\n"
 "add $17,%r8\n"
 "xor $0x12345,%r8\n"
 "mov %r8,%r9\n"
 "lea 37(%rax,%r8,2),%rax\n"
 "add $0x7788,%r9\n"
 "mov %r9d,%r9d\n"
 "rol $7,%r9d\n"
 "add %r9,%rax\n"
 "sub $25,%rax\n"
 "mov $0,%r11\n"
 ".global chain_fault\n"
 "chain_fault: mov (%r11),%r10\n"
 "ret\n");
}
static LONG CALLBACK handler(EXCEPTION_POINTERS *p)
{
 CONTEXT *c=p->ContextRecord;
 if(p->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || p->ExceptionRecord->ExceptionAddress!=&chain_fault)
  return EXCEPTION_CONTINUE_SEARCH;
 if(c->Rax!=want_a || c->R8!=want_b || c->R9!=want_c) {
  if(!bad) fprintf(stderr,"context got %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " expected %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",c->Rax,c->R8,c->R9,want_a,want_b,want_c);
  bad=1;
 }
 /* Last flag-setting instruction subtracts 25 from the expected pre-value. */
 uint64_t before=want_a+25;unsigned f=2;
 if(before<25)f|=1;if(!want_a)f|=0x40;if(want_a>>63)f|=0x80;
 if((before^25^want_a)&16)f|=0x10;
 if(((before^25)&(before^want_a))>>63)f|=0x800;
 unsigned char low=(unsigned char)want_a;unsigned parity=0;for(int i=0;i<8;i++)parity^=(low>>i)&1;if(!parity)f|=4;
 if((c->EFlags&0x8d7)!=f) {if(!bad)fprintf(stderr,"flags got %lx expected %x\n",c->EFlags,f);bad=1;}
 faults++;c->Rip+=3;return EXCEPTION_CONTINUE_EXECUTION;
}
static volatile LONG stop_worker;
extern char worker_loop,worker_end;
__attribute__((naked,noinline)) static DWORD WINAPI worker(void *unused)
{
 __asm__ volatile(
 "mov $1,%rax\n"
 ".global worker_loop\n"
 "worker_loop: mov %rax,%r8\n"
 "xor $0x12345,%r8\n"
 "mov %rax,%r9\n"
 "add $37,%r9\n"
 "inc %rax\n"
 "cmpl $0,(%rcx)\n"
 "je worker_loop\n"
 ".global worker_end\n"
 "worker_end: xor %eax,%eax\n"
 "ret\n");
}
static int suspension_probe(void)
{
 HANDLE t=CreateThread(NULL,0,worker,(void*)&stop_worker,0,NULL);unsigned checked=0;
 if(!t)return 1;
 Sleep(10);
 for(unsigned i=0;i<500;i++){
  CONTEXT c={0};c.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;
  if(SuspendThread(t)==(DWORD)-1)return 2;
  BOOL ok=GetThreadContext(t,&c);
  if(!ResumeThread(t) || !ok)return 3;
  if(c.Rip>=(uintptr_t)&worker_loop && c.Rip<(uintptr_t)&worker_end){
   checked++;
   if(c.R8!=((c.Rax-1)^0x12345) || c.R9!=c.Rax-1+37){
    fprintf(stderr,"suspended context relation failed: rip=%llx rax=%llx r8=%llx r9=%llx\n",c.Rip,c.Rax,c.R8,c.R9);
    InterlockedExchange(&stop_worker,1);WaitForSingleObject(t,10000);CloseHandle(t);return 4;
   }
  }
 }
 InterlockedExchange(&stop_worker,1);DWORD result=WaitForSingleObject(t,10000);CloseHandle(t);
 printf("cooperative suspend/context register relations: %s (%u checked)\n",checked>400 && result==WAIT_OBJECT_0?"PASS":"FAIL",checked);
 return checked<=400 || result!=WAIT_OBJECT_0;
}
int main(void)
{
 if(!AddVectoredExceptionHandler(1,handler))return 2;
 uint64_t seed=0x1122334455667788ULL;
 for(unsigned i=0;i<10000;i++) {
  seed=seed*6364136223846793005ULL+1442695040888963407ULL;
  want_b=(seed+17)^0x12345;uint32_t narrow=(uint32_t)(want_b+0x7788);
  want_c=(uint32_t)((narrow<<7)|(narrow>>25));want_a=seed+2*want_b+37+want_c-25;
  if(chain(seed)!=want_a)bad=1;
 }
 printf("register chains, 32-bit zero extension and precise fault flags/context: %s (%ld faults)\n",!bad&&faults==10000?"PASS":"FAIL",faults);
 if(suspension_probe())bad=1;
 if(getenv("HOLD_PROBE_SECONDS")){fflush(stdout);Sleep(atoi(getenv("HOLD_PROBE_SECONDS"))*1000);}
 return bad||faults!=10000;
}
