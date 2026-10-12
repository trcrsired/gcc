/* { dg-do compile } */
/* { dg-options "-fherbceptions -std=c2x" } */
/* try(expr) requires the operand to be a call to a return_failure{E}
   function; catch return_failure(expr) likewise.  */

int f (int x) return_failure{int} { return x; }

int
not_a_call (int x) return_failure{int}
{
  return try (f (x) + 1); /* { dg-error "requires a call to a function declared" } */
}

int
not_a_spec_call (int x) return_failure{int}
{
  return try (x); /* { dg-error "requires a call to a function declared" } */
}

int
catch_not_a_call (int x)
{
  int e = 0;
  __typeof__ (catch return_failure (f (0))) r
    = catch return_failure (e + f (x)); /* { dg-error "requires a call to a function declared" } */
  return r.failed;
}
