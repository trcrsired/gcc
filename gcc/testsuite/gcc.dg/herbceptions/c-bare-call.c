/* { dg-do compile } */
/* { dg-options "-fherbceptions -std=c2x" } */
/* Port of clang's herbceptions-c-bare-call.c.  In C, calling a
   return_failure{E} function without try() or catch
   return_failure() is a compile-time error.  */

int bar (int x) return_failure{int} { return x; }

int
foo (int x)
{
  return bar (x); /* { dg-error "requires .try\\(\\) or .catch return_failure\\(\\)" } */
}
