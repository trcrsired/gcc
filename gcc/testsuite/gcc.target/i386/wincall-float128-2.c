/* `_Complex _Float128' is thirty-two bytes of scalar floating-point: each
   component would fit one XMM register, but the convention keeps a complex
   value in a single register or in none at all, exactly as it does for
   `_Complex long double'.  It is therefore passed and returned by
   invisible reference, matching clang: the result comes back through the
   pointer clang passes in RCX and the argument through the pointer in RDX.

   The test is asm-level because this box is a Zen 2 and cannot execute
   APX.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2 -mapxf" } */

typedef _Complex _Float128 cf128;

__attribute__ ((wincall, noinline, noipa)) cf128
cq_ret (cf128 v)
{
  return v;
}

__attribute__ ((wincall, noinline, noipa)) cf128
cq_arg (cf128 a, cf128 b)
{
  return a + b;
}

/* Both halves of the incoming value are read through the argument pointer
   in RDX, and both halves of the result are written through the result
   pointer in RCX, which is handed back in RAX.  */
/* { dg-final { scan-assembler "\\(%rdx\\)" } } */
/* { dg-final { scan-assembler "16\\(%rcx\\)" } } */
/* { dg-final { scan-assembler "movq\t%rcx, %rax" } } */
