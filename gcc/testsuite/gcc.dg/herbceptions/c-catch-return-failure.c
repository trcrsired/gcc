/* { dg-do run { target { *-*-linux* } } } */
/* { dg-options "-fherbceptions -fno-exceptions" } */
/* `catch return_failure(expr)' yields the N2289 aggregate
   `struct { union { T value; E error; }; _Bool failed; }'; the either
   type is canonicalized so __typeof__ and a repeated use agree.  */

int
f (int x) return_failure{int}
{
  if (x < 0)
    return_failure 5;
  return x;
}

int
main (void)
{
  __typeof__ (catch return_failure (f (0))) e = catch return_failure (f (-1));

  if (!e.failed || e.error != 5)
    return 1;

  e = catch return_failure (f (42));
  if (e.failed || e.value != 42)
    return 2;

  return 0;
}
