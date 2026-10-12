/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions" } */
/* Function types carrying a `return_failure{E}' specifier are distinct
   from plain function types and from specifiers with a different error
   type: no implicit conversions in either direction.  */

int f (int x) return_failure{int} { return x; }
int g (int x) return_failure{long} { return x; }

int (*plain) (int);
int (*spec_int) (int) return_failure{int};
int (*spec_long) (int) return_failure{long};

void
test (void)
{
  plain = f;			/* { dg-error "incompatible pointer type" } */
  spec_int = plain;		/* { dg-error "incompatible pointer type" } */
  spec_int = f;			/* ok */
  spec_int = g;			/* { dg-error "incompatible pointer type" } */
  spec_long = f;		/* { dg-error "incompatible pointer type" } */
  spec_int = spec_long;		/* { dg-error "incompatible pointer type" } */
}
