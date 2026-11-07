/* { dg-do run { target { *-*-linux* } } } */
/* { dg-options "-fherbceptions -fno-exceptions -O2" } */
/* The C channel has the same rule as the C++ one: what a return_failure{E}
   function returns escapes through the return, so the stores that initialize
   memory reachable only from the returned pointer must survive.  See
   g++.dg/herbceptions/newpayload1.C for the C++ side.
   Regression test for bugs/gcc_herbceptions/new-payload-dse.  */

#include <stdlib.h>

struct S
{
  double x;
};

static struct S *
make (int n) return_failure{int}
{
  struct S *p;

  if (n < 0)
    return_failure 3;
  p = malloc (sizeof (struct S));
  p->x = 7.0;
  return p;
}

int
main (void)
{
  __typeof__ (catch return_failure (make (0))) ok = catch return_failure (make (0));
  __typeof__ (catch return_failure (make (0))) bad = catch return_failure (make (-1));
  double got = ok.value->x;

  free (ok.value);
  if (ok.failed || got != 7.0)
    return 1;
  if (!bad.failed || bad.error != 3)
    return 2;
  return 0;
}
