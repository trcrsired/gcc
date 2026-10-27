/* The herbceptions return-flag folding pass rewrites the discriminant
   reload on every edge into the merged return block with a direct CF
   write when the edge statically implies the value.  On x86-64 the
   discriminant lives in EFLAGS.CF, so the success and failure returns
   become `clc' and `stc' and no `negb' reload remains.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-options "-fherbceptions -fno-exceptions -O2" } */

int
f (int x) return_failure{long}
{
  if (x < 0)
    return_failure 1;
  return x;
}

/* { dg-final { scan-assembler "clc" } } */
/* { dg-final { scan-assembler "stc" } } */
/* { dg-final { scan-assembler-not "negb" } } */
