// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -std=c++26" }
// Port of clang's herbceptions-catch-fails-nontrivial.cpp: a
// `catch return_failure' whose success type has a non-trivial destructor must
// be accepted, and a void success type must still yield the three-member
// either aggregate (GCC gives the `value' member the error type for a void
// success).  Regression test for
// bugs/llvm_herbceptions/catch-rf-nontrivial-dtor.

struct T {
  int v;
  ~T () {}
};
struct E {
  int e;
};

T
f (int n) return_failure{E}
{
  if (n < 0)
    return_failure E{-n};
  return {n};
}

int
g (int n)
{
  auto r = catch return_failure (f (n));
  return r.failed ? r.error.e : r.value.v;
}

void
v (bool fail) return_failure{E}
{
  if (fail)
    return_failure E{7};
}

int
main ()
{
  if (g (5) != 5)
    return 1;
  if (g (-1) != 1)
    return 2;

  auto r = catch return_failure (v (false));
  if (r.failed)
    return 3;
  auto r2 = catch return_failure (v (true));
  if (!r2.failed || r2.error.e != 7)
    return 4;

  return 0;
}
