/* { dg-do run { target { *-*-linux* } } } */
/* { dg-options "-fherbceptions -fno-exceptions" } */
/* The return_failure{E} spec travels with the function type through a
   typedef/function pointer, and void-returning spec'd functions carry
   only the error payload.  */

typedef int (*fp) (int) return_failure{int};

int
f (int x) return_failure{int}
{
  if (x < 0)
    return_failure 3;
  return x;
}

int
viaptr (fp p, int x) return_failure{int}
{
  return try (p (x));
}

void
v (int x) return_failure{int}
{
  if (x < 0)
    return_failure 8;
}

int
use_v (int x)
{
  __typeof__ (catch return_failure (v (0))) e
    = catch return_failure (v (x));
  if (e.failed)
    return e.error;
  return 0;
}

int
main (void)
{
  __typeof__ (catch return_failure (viaptr (f, 0))) e
    = catch return_failure (viaptr (f, -2));
  if (!e.failed || e.error != 3)
    return 1;

  e = catch return_failure (viaptr (f, 7));
  if (e.failed || e.value != 7)
    return 2;

  if (use_v (-1) != 8)
    return 3;

  return 0;
}
