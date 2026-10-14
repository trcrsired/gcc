/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O2" } */

/* The same as herbceptions-disc.c at -O2, where the failure flag is no
   longer an ordinary memory reference and the load of %icc is only
   separated from the call by the argument setup that the delay slot
   filler may move.  See herbceptions-disc.c for the ABI.  */

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

/* The callee still sets the carry from the failure flag ...  */
/* { dg-final { scan-assembler "cmp\\t%g0," } } */
/* ... and the caller still materializes it straight after the call.  */
/* { dg-final { scan-assembler "call\\text" } } */
/* { dg-final { scan-assembler "addx\\t%g0, 0," } } */
