/* The return-flag folding pass duplicates small return blocks, so it is
   disabled for size-optimized functions (and at -O0): the discriminant
   keeps being materialized and re-loaded into CF.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-options "-fherbceptions -fno-exceptions -Os" } */

int
f (int x) return_failure{long}
{
  if (x < 0)
    return_failure 1;
  return x;
}

/* { dg-final { scan-assembler "negb" } } */
