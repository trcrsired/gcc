/* The herbceptions return-flag folding pass rewrites the discriminant
   reload on every edge into the merged return block with a direct NZCV.C
   write when the edge statically implies the value.  On AArch64 the
   discriminant lives in NZCV.C, so a success return becomes
   `cmn wzr, wzr' (C = 0) and a failure return `cmp wzr, wzr' (C = 1).  */

/* { dg-do assemble } */
/* { dg-options "-fherbceptions -fno-exceptions -O2" } */

int
f (int x) return_failure{long}
{
  if (x < 0)
    return_failure 1;
  return x;
}

/* Failure path: C = 1.  */
/* { dg-final { scan-assembler "cmp\\twzr, wzr" } } */
/* Success path: C = 0.  */
/* { dg-final { scan-assembler "cmn\\twzr, wzr" } } */
/* Nothing is left to reload from a register.  */
/* { dg-final { scan-assembler-not "cmp\\tw\[0-9\]+, #1" } } */
