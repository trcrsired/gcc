/* { dg-do run { target { *-*-linux* } } } */
/* { dg-options "-fherbceptions -fno-exceptions" } */
/* A payload wider than the error record returns in memory while the
   error rides the channel; the success path must read the value
   storage, not the error view.  */

typedef struct
{
  long long a, b, c;
} Big;

Big
big (int n) return_failure{int}
{
  Big b;
  if (n < 0)
    return_failure (-n);
  b.a = n;
  b.b = n + 1;
  b.c = n + 2;
  return b;
}

int
main (void)
{
  __typeof__ (catch return_failure (big (0))) e
    = catch return_failure (big (1));

  if (e.failed || e.value.a != 1 || e.value.b != 2 || e.value.c != 3)
    return 1;

  e = catch return_failure (big (-5));
  if (!e.failed || e.error != 5)
    return 2;

  return 0;
}
