/* { dg-do compile } */
/* { dg-options "-fherbceptions -std=c2x" } */
/* Port of clang's herbceptions-c-try-expr.c.  In C, try(expr) is
   mandatory around a return_failure{E} call — even inside another
   return_failure function — and is only allowed inside a function
   declared return_failure{...}.  */

int f (int x) return_failure{int};

int
ok_in_return_failure (int x) return_failure{int}
{
  return try (f (x));
}

int
bad_in_return_failure (int x) return_failure{int}
{
  return f (x); /* { dg-error "requires .try\\(\\) or .catch return_failure\\(\\)" } */
}

int
bad_plain (int x)
{
  return f (x); /* { dg-error "requires .try\\(\\) or .catch return_failure\\(\\)" } */
}

/* C has no 'throws' specifier, so the diagnostic must not mention it.  */
int
bad_try_plain (int x)
{
  return try (f (x)); /* { dg-error "only allowed inside a function declared" } */
}

/* try{} / catch are C++-only: in C exceptions are disabled.  */
int
bad_try_block (int x)
{
  try /* { dg-error "cannot use .try. with exceptions disabled" } */
    {
      return f (x); /* { dg-error "requires .try\\(\\) or .catch return_failure\\(\\)" } */
    }
  catch (int e) /* { dg-error "cannot use .catch. with exceptions disabled" } */
    {
      return e;
    }
}
