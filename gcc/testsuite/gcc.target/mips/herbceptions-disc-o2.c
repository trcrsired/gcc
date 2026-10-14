/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O2" } */

/* The same as herbceptions-disc.c at -O2, where the failure flag is no
   longer an ordinary memory reference: the write into $4 has to survive
   optimization even though nothing in the compiled function appears to
   read it, and the caller has to read it back before anything else has a
   chance to reuse the register.  See herbceptions-disc.c for the ABI.  */

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

/* The callee still writes the discriminant into $4 ...  */
/* { dg-final { scan-assembler "andi\\t\[\$]4," } } */
/* ... and the caller still reads it straight after the call.  */
/* { dg-final { scan-assembler "jal\\text" } } */
/* { dg-final { scan-assembler "andi\\t\[\$]3,\[\$]4,1" } } */
