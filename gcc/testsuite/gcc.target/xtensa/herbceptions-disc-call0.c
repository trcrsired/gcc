/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O0 -mabi=call0" } */

/* Under the call0 ABI there is no register window rotation, so the
   caller observes the callee's discriminant in its own a4 rather than in
   its own a12.  See herbceptions-disc.c for the rest.  */

extern int ext (int) return_failure{long};

int
callee (int x) return_failure{long}
{
  if (x < 0)
    return_failure 1;
  return x + 1;
}

int
caller (int x) return_failure{long}
{
  return try (ext (x));
}

/* The callee leaves the discriminant in a4 ...  */
/* { dg-final { scan-assembler "mov\\ta4, a" } } */
/* ... and the caller reads it back from its own a4, with no window.  */
/* { dg-final { scan-assembler "call0\\text" } } */
/* { dg-final { scan-assembler "mov\\.n\\ta[0-9]+, a4" } } */
