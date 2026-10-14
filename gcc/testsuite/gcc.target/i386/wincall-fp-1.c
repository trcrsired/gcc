/* WinCall allocates the integer and the FP/SIMD argument registers
   independently, unlike the Microsoft x64 ABI, which pairs each integer
   register with an XMM register and skips the partner when the other
   class is used.  `f' therefore takes its three ints in ECX, EDX, R8D
   and its three doubles in XMM0, XMM1, XMM2; an MS-style allocator would
   have used XMM0, XMM2, XMM4 and skipped the odd XMMs.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2 -mapxf" } */

__attribute__ ((wincall, noinline))
double
f (int a, double b, int c, double d, int e, double g)
{
  return b + d + g + a + c + e;
}

/* { dg-final { scan-assembler "%xmm1" } } */
/* { dg-final { scan-assembler "%xmm2" } } */
/* { dg-final { scan-assembler-not "%xmm4" } } */
/* { dg-final { scan-assembler "%r8d" } } */
