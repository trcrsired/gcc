/* Same body and flags as wincall-callee-saved-1.c, but SysV.  R30 and
   R31 belong to the caller there, so the value has to be spilled rather
   than kept across the call, and no R30/R31 may appear at all.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2 -mapxf -ffixed-bx -ffixed-bp -ffixed-r12 -ffixed-r13 -ffixed-r14 -ffixed-r15 -ffixed-si -ffixed-di" } */
/* { dg-final { scan-assembler-not "%r3\[01\]" } } */

extern long c (long);

long
sv (long x)
{
  long r = c (x);
  return r + x;
}
