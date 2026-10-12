// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-try-bind-temp: a bare call whose result is
// a class with a nontrivial destructor carries a bind-temporary
// wrapper — the lowering must strip it to reach the call.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

struct S {
  int v;
  ~S () {}
};

S
make_s () throws
{
  return {1};
}

struct T {
  int v;
  ~T () {}
};

T
make_t (int n) return_failure{int}
{
  if (n < 0)
    return_failure -n;
  return {n};
}

void
take () throws
{
  make_s ();
}

T
take2 () return_failure{int}
{
  return make_t (-41);
}

int
main ()
{
  try
    {
      take ();
    }
  catch throws (std::error)
    {
      return 1;
    }
  auto e = catch return_failure (take2 ());
  return (e.failed && e.error == 41) ? 0 : 3;
}
