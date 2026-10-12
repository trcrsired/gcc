/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O0" } */
/* The aarch64 herbceptions error-channel discriminant rides in NZCV.C:
   the callee glues `cmp w16, #1' to the `ret' (so the epilogue's
   stack-protector/writeback flag use cannot clobber it) and the caller
   reads it with `cset ..., cs' right after the call.  */

int
f (int x) return_failure{long}
{
  if (x < 0)
    return_failure 1;
  return x;
}

int
g (int x) return_failure{long}
{
  return try (f (x));
}

/* { dg-final { scan-assembler "cset\\tw\[0-9\]+, cs" } } */
/* { dg-final { scan-assembler "cmp\\tw16, #1" } } */
