// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2" }
// The payload of a spec'd function's return channel has to escape through
// the return: the caller's only handle on a freshly allocated object is the
// pointer that rides the channel.  The channel local is volatile and is read
// only at RTL expansion time, so nothing in GIMPLE reads it, and the
// `.HERBCEPTION_RETURN_PAYLOAD' carrier that replaces the store into it has
// no memory operands at all (ECF_NOVOPS).  Without an explicit
// escaped-for-return constraint the pointee looks dead and DSE deletes the
// stores that initialize it, so `make (7.0)' returned a pointer to
// uninitialized memory at -O1 and above.
// Regression test for bugs/gcc_herbceptions/new-payload-dse.

struct S
{
  double x;
};

S *
make (double v) throws
{
  return new S {v};
}

/* The same thing written as store-then-return, so that the initialized
   object is also a named local before it is returned.  */

S *
make_staged (double v) throws
{
  S *p = new S {0.0};
  p->x = v;
  return p;
}

int
main ()
{
  S *a = make (7.0);
  S *b = make_staged (9.0);
  double const got_a = a->x;
  double const got_b = b->x;
  delete a;
  delete b;
  if (got_a != 7.0 || got_b != 9.0)
    return 1;
  return 0;
}
