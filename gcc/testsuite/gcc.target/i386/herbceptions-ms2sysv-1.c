/* Herbceptions combined with the MS-ABI outlined x86-64 call stubs
   (-mcall-ms2sysv-xlogues).  A SysV call from an MS-ABI function is
   routed through __sse_savms64/__sse_resms64 helpers, which clobber CF,
   so the failure discriminant has to be written after the last such
   call.  The flag-folding pass emits `clc'/`stc' before the tail `jmp'
   to the restore helper, and that jump must preserve CF.  */

/* { dg-do run { target { x86_64-*-linux* } } } */
/* { dg-options "-O2 -fherbceptions -fno-exceptions -mcall-ms2sysv-xlogues" } */

typedef float v4sf __attribute__ ((vector_size (16)));

__attribute__ ((sysv_abi, noinline, noipa))
void
sysv_fn (v4sf v)
{
  __asm__ volatile ("" : : "x" (v) : "memory");
}

__attribute__ ((ms_abi, noinline, noipa))
int
f (int x) return_failure{long}
{
  v4sf v = {1, 2, 3, 4};
  sysv_fn (v);
  if (x < 0)
    return_failure 1;
  return x + 1;
}

int
main (void)
{
  __typeof__ (catch return_failure (f (0))) a
    = catch return_failure (f (41));
  if (a.failed || a.value != 42)
    return 1;
  __typeof__ (catch return_failure (f (0))) b
    = catch return_failure (f (-7));
  if (!b.failed || b.error != 1)
    return 2;
  return 0;
}

/* The xlogue helpers must actually be in the code path.  */
/* { dg-final { scan-assembler "__sse_savms64" } } */
/* { dg-final { scan-assembler "__sse_resms64" } } */
/* The folded flag writes are present on both return edges.  */
/* { dg-final { scan-assembler "clc" } } */
/* { dg-final { scan-assembler "stc" } } */
/* The discriminant is folded into CF, not reloaded from the slot.  */
/* { dg-final { scan-assembler-not "negb" } } */
