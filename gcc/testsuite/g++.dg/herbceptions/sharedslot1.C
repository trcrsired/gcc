// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2" }
//
// A class member initialised from a `throws' call gets the return slot
// optimisation: the call's by-reference result is passed the address of
// the member (`this->s'), a COMPONENT_REF.  When the callee is inlined,
// the inliner inserts a `retvalptr = &this->s' statement *and* remaps
// the callee's own materialisation of the channel payload, which the
// frontend emits as `this->s = VIEW_CONVERT_EXPR<S> (__hb_result)'.
// Both statements used to refer to the very same COMPONENT_REF node, so
// verify_gimple rejected the function with "incorrect sharing of tree
// nodes" as soon as early inlining ran (reported against
// accept_loop() in fast_io's http-server.cc, but reproducible without
// coroutines).  The inserted statement must use its own copy of the
// destination expression.
// Regression test for bugs/gcc_herbceptions/inliner-shares-return-slot.

namespace std
{
  struct error
  {
    const void *domain;
    __UINTPTR_TYPE__ code;
  };
}

struct S
{
  int v;
  S (int x) : v (x) {}
  S (const S &o) : v (o.v) {}
  ~S () {}
};

struct W
{
  S s;
  W (int n) throws;
};

S
mk (int n) throws
{
  if (n < 0)
    throw throws (std::error{(const void *) 0x1234, 7});
  return S (n);
}

W::W (int n) throws : s (mk (n)) {}

int
main ()
{
  W w (42);
  if (w.s.v != 42)
    return 1;

  bool caught = false;
  try
    {
      W b (-1);
      (void) b;
    }
  catch throws (::std::error e)
    {
      caught = (e.domain == (const void *) 0x1234 && e.code == 7);
    }
  return caught ? 0 : 2;
}
