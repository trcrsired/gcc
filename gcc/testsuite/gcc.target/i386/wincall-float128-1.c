/* WinCall passes and returns `_Float128' in one XMM register, like the
   other floating-point scalars, rather than splitting it into the integer
   register pair RCX:RDX the way `__int128' is split -- and rather than
   returning it through an invisible reference, which is what GCC used to
   do.  clang, GCC and docs/WinCall.md agree on the XMM rule.  The rule
   also covers an aggregate whose natural mode is a floating-point mode
   that fits one XMM register, i.e. a struct with a single `_Float128'
   member: retsq returns it in XMM0, not through a hidden pointer.

   `_Complex _Float128' is the exception and is passed and returned by
   invisible reference, as clang does; see wincall-float128-2.c.

   The test is asm-level because this box is a Zen 2 and cannot execute
   APX.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2 -mapxf" } */

typedef _Float128 f128;

struct sq
{
  f128 q;
};

__attribute__ ((wincall, noinline, noipa)) f128
tf_ret (f128 v)
{
  return v + 1;
}

__attribute__ ((wincall, noinline, noipa)) f128
tf_arg (f128 a, f128 b)
{
  return a + b;
}

__attribute__ ((wincall, noinline, noipa)) struct sq
retsq (struct sq s)
{
  s.q += 1;
  return s;
}

/* Eight `_Float128' arguments fill XMM0-XMM7, so the ninth lands in the
   first stack slot.  */

__attribute__ ((wincall, noinline, noipa)) f128
n9 (f128 a, f128 b, f128 c, f128 d, f128 e, f128 g, f128 h, f128 i, f128 j)
{
  return j;
}

/* None of the integer argument or return registers takes part, and no
   value is passed or returned through memory.  */
/* { dg-final { scan-assembler-not "%rcx" } } */
/* { dg-final { scan-assembler-not "%rdx" } } */
/* { dg-final { scan-assembler-not "%r8" } } */
/* { dg-final { scan-assembler-not "%r1\[6-9\]" } } */
/* The ninth `_Float128' argument is read from the stack.  */
/* { dg-final { scan-assembler "8\\(%rsp\\)" } } */
