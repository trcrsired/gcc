/* Herbceptions over WinCall, C side.  A `return_failure{E}' channel on a
   WinCall function returns its payload in the WinCall return registers
   and keeps the failure discriminant in CF; the caller therefore has to
   read CF right after the call, exactly as it does for the SysV/MS
   conventions.  asm-level only: the convention requires -mapxf and this
   box cannot execute APX.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2 -mapxf -fherbceptions -fno-exceptions" } */

struct Err { int e; };

__attribute__ ((wincall, noinline))
int
c (int x) return_failure{struct Err}
{
  if (x < 0)
    return_failure (struct Err){ .e = -x };
  return x;
}

int
caller (int x) return_failure{struct Err}
{
  return try (c (x)) + 1;
}

/* The callee has a constant discriminant on each return edge, so the
   return-flag folding pass writes CF directly to 0 or 1 there instead of
   materialising the value and re-loading it with `negb'.  */
/* { dg-final { scan-assembler "clc" } } */
/* { dg-final { scan-assembler "stc" } } */

/* The caller's discriminant comes from the call, so it cannot be folded
   and still needs the CF-setting negate.  */
/* { dg-final { scan-assembler "negb" } } */

/* The caller reads CF immediately after the call, before anything that
   could clobber it.  The callee is WinCall, so on PE it also carries the
   `@win' suffix.  */
/* { dg-final { scan-assembler "call\tc(@win)?\n" } } */
/* { dg-final { scan-assembler "setc" } } */
