/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O2" } */

/* The same as herbceptions-disc.c at -O2, where the failure flag is no
   longer an ordinary memory reference: the move into a4 has to survive
   optimization even though nothing in the compiled function appears to
   read it, and the caller has to read the leaf back before anything else
   has a chance to reuse the register.  See herbceptions-disc.c.  */

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

/* The callee still writes the discriminant into a4 ...  */
/* { dg-final { scan-assembler "mov\\ta4, a" } } */
/* ... and the caller still reads the leaf straight after the call.  */
/* { dg-final { scan-assembler "call8\\text" } } */
/* { dg-final { scan-assembler "a12" } } */
