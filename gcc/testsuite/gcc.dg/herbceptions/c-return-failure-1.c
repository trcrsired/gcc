/* { dg-do run { target { *-*-linux* } } } */
/* { dg-options "-fherbceptions -fno-exceptions" } */
/* Port of clang's herbceptions-c-try-expr.c runtime behaviour: in C a
   `return_failure{E}' call must always be wrapped in try()/catch
   return_failure(); the error channel propagates through try() inside
   another return_failure function.  */

int
f (int x) return_failure{int}
{
  if (x < 0)
    return_failure 7;
  return x;
}

int
g (int x) return_failure{int}
{
  int v = try (f (x));
  if (v > 100)
    return_failure 9;
  return v + 1;
}

int
main (void)
{
  __typeof__ (catch return_failure (g (0))) e;

  e = catch return_failure (g (-1));
  if (!e.failed || e.error != 7)
    return 1;

  e = catch return_failure (g (200));
  if (!e.failed || e.error != 9)
    return 2;

  e = catch return_failure (g (41));
  if (e.failed || e.value != 42)
    return 3;

  return 0;
}
