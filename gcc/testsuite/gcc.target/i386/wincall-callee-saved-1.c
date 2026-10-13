/* WinCall additionally preserves R30 and R31, so a value that has to
   survive a call can live in one of them.  Every other callee-saved
   register is pinned out of the allocator here, which leaves R30/R31 as
   the only callee-saved registers -- the prologue therefore has to save
   one of them.  The SysV twin in wincall-callee-saved-2.c has the same
   body and the same flags but must spill instead, because R30/R31 are
   call-clobbered there.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2 -mapxf -ffixed-bx -ffixed-bp -ffixed-r12 -ffixed-r13 -ffixed-r14 -ffixed-r15 -ffixed-si -ffixed-di" } */
/* { dg-final { scan-assembler "%r3\[01\]" } } */

extern long c (long);

__attribute__ ((wincall)) long
wc (long x)
{
  long r = c (x);
  return r + x;
}
