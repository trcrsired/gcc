/* The APX EGPR implication of the WinCall convention is per function: only
   the WinCall function itself and the functions that call it get the extra
   registers.  A translation unit that merely *declares* a WinCall function
   -- or that takes its address without calling it -- keeps the translation
   unit defaults, so unrelated code is not compiled with APX and still runs
   on hardware without it.  This is the rule clang implements with "+egpr"
   in X86TargetMachine::getSubtargetImpl; wincall-implies-apx-1.c checks the
   positive half.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2" } */

extern long wc (long, long, long, long, long) __attribute__ ((wincall));

typedef long (*wcfn) (long, long, long, long, long) __attribute__ ((wincall));
wcfn held = wc;

/* High register pressure: with EGPR available this function reaches into
   R16-R31, so finding none of those registers in the output proves wincall
   did not enable EGPR here.  */
long
unrelated (long a0, long a1, long a2, long a3, long a4, long a5, long a6,
	   long a7, long a8, long a9, long a10, long a11, long a12, long a13,
	   long a14, long a15)
{
  return (a0 * 3 + a1 * 4 + a2 * 5 + a3 * 6 + a4 * 7 + a5 * 8 + a6 * 9
	  + a7 * 10 + a8 * 11 + a9 * 12 + a10 * 13 + a11 * 14 + a12 * 15
	  + a13 * 16 + a14 * 17 + a15 * 18);
}

/* { dg-final { scan-assembler-not "%r16" } } */
/* { dg-final { scan-assembler-not "%r17" } } */
/* { dg-final { scan-assembler-not "%r18" } } */
/* { dg-final { scan-assembler-not "%r19" } } */
/* { dg-final { scan-assembler-not "%r2\[0-9\]" } } */
/* { dg-final { scan-assembler-not "%r3\[01\]" } } */
