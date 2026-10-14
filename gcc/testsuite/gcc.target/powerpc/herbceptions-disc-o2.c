/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O2" } */

/* The same as herbceptions-disc.c at -O2, where the failure flag is no
   longer an ordinary memory reference: the compare into cr6.GT has to
   survive optimization even though nothing in the compiled function
   appears to read it, and the caller has to extract the field before
   anything else has a chance to rewrite it.  See herbceptions-disc.c.  */

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

/* The callee still sets cr6.GT from the flag ...  */
/* { dg-final { scan-assembler "cmp\[wd\]i 6," } } */
/* ... and the caller still reads the field straight after the call.  */
/* { dg-final { scan-assembler "bl ext" } } */
/* { dg-final { scan-assembler "mfcr \[0-9\]" } } */
/* { dg-final { scan-assembler "rlwinm \[0-9\]+,\[0-9\]+,26,31,31" } } */
