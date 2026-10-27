/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O2" } */

/* The same as herbceptions-disc.c at -O2, where the failure flag is no
   longer an ordinary memory reference: the herbceptions flag-folding
   pass knows the discriminant from the branch and sets C with `sec' on
   the failure return and `clc' on the successful one.  The caller still
   reads C back with the movb/adcb pair straight after the call.  */

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

/* The failure return sets C directly ...  */
/* { dg-final { scan-assembler "\\tsec" } } */
/* ... the successful return clears it ...  */
/* { dg-final { scan-assembler "\\tclc" } } */
/* ... the caller reads it back ...  */
/* { dg-final { scan-assembler "\\tadcb" } } */
/* ... the 16-bit success value is widened into the single 32-bit
   channel value (two `int' bytes plus the error's), which is carried in
   one SImode value rather than split into word-sized pieces ...  */
/* { dg-final { scan-assembler "\\tsxt" } } */
/* ... and the frame teardown preserves it.  */
/* { dg-final { scan-assembler "mov\tr5,sp" } } */
