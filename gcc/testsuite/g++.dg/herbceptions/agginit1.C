// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2" }
// This used to fail at -O2: the post-reload if-conversion pass hoisted
// main's `return 3' ahead of the CF branch and clobbered %rax while the
// error payload was still live on the failure edge (see
// bugs/gcc_herbceptions/ifcvt-hoist-clobbers-channel).  The discriminant
// is now consumed by a branch straight on the carry flag rather than by
// a `setc'/`testb' pair, so there is no flag-register value left for
// if-conversion to sink the failure payload around.
// A declaration's initializer that calls a `throws' function returning a
// non-trivially-copyable class makes the frontend emit a raw
// AGGR_INIT_EXPR wrapped in the implicit herbceptions try.  TREE_TYPE of
// that node is void and its result slot is the frontend's own temp with
// no DECL_EXPR (the gimplifier only knows how to elide it when the
// AGGR_INIT_EXPR is the direct RHS of an assignment).  The lowering used
// to read the slot past the call, which ICE'd in gimplify_var_or_parm_decl;
// it must materialize the object into a declared channel-local instead.
// Regression test for bugs/gcc_herbceptions/agginit-void-slot.

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

S
make (int x) throws
{
  if (x < 0)
    throw throws (std::error{(const void *) 0x1234, 7});
  return S (x);
}

S
init_from_call (int x) throws
{
  S s = make (x); /* the declaration initializer */
  return s;
}

int
main ()
{
  S a = init_from_call (42);
  if (a.v != 42)
    return 1;

  bool caught = false;
  try
    {
      S b = init_from_call (-1);
      (void) b;
    }
  catch throws (::std::error e)
    {
      caught = true;
      if (e.domain != (const void *) 0x1234 || e.code != 7)
	return 2;
    }
  if (!caught)
    return 3;

  return 0;
}
