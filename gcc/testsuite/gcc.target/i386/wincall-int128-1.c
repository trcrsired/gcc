/* WinCall returns a 128-bit integer in RAX (low) and RDX (high), and
   takes it in RCX (low) and RDX (high), exactly as the SysV ABI, clang
   and docs/WinCall.md do.  The backend used to send a 128-bit integer
   return through an invisible reference instead -- which also pushed the
   argument pair one register over, so the caller and the callee
   disagreed and a gcc-callee/clang-caller round trip crashed.  A 128-bit
   integer is not an aggregate, so the eight-byte-chunk return path of
   wincall-return-1.c never applied to it.

   The test is asm-level because this box is a Zen 2 and cannot execute
   APX.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2 -mapxf" } */

__attribute__ ((wincall, noinline, noipa)) __int128
ri (__int128 v)
{
  return v + 1;
}

__attribute__ ((wincall, noinline, noipa)) unsigned __int128
rui (unsigned __int128 v)
{
  return v + 1;
}

/* Three eight-byte pairs: RCX:RDX, R8:R9 and R16:R17, and the sum is
   accumulated into RAX:RDX.  */

__attribute__ ((wincall, noinline, noipa)) __int128
add3 (__int128 a, __int128 b, __int128 c)
{
  return a + b + c;
}

/* The low half is read from RCX and handed back in RAX ...  */
/* { dg-final { scan-assembler "movq\t%rcx, %rax" } } */
/* ... and the high half from RDX, which also carries it back out, so
   there is no hidden pointer argument or store.  */
/* { dg-final { scan-assembler "adcq\t\\\$0, %rdx" } } */
/* { dg-final { scan-assembler-not "\\(%rcx\\)" } } */
/* unsigned __int128 shares the mode of __int128.  */
/* { dg-final { scan-assembler-times "adcq\t\\\$0, %rdx" 2 } } */

/* The third pair is R16:R17, and it is added into RAX:RDX.  */
/* { dg-final { scan-assembler "addq\t%r16, %rax" } } */
/* { dg-final { scan-assembler "adcq\t%r17, %rdx" } } */
