// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-throws-structor: a spec'd call in a ctor's
// member initializer propagates through the ctor's channel; explicit
// throw throws in a ctor body works; bare early return works.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

int g_code;

int
may_fail () throws
{
  if (g_code)
    throw throws (std::error{(const void *) 0x4, (__UINTPTR_TYPE__) g_code});
  return 7;
}

struct Widget {
  int x;
  Widget () throws : x (may_fail ()) {}
};

struct Other {
  int x;
  Other () throws { x = may_fail (); }
};

struct Manual {
  int x;
  Manual (int bad) throws
  {
    if (bad < 0)
      throw throws (std::error{nullptr, 1});
    x = bad;
  }
};

void
early_exit () throws
{
  return;
}

int
main ()
{
  g_code = 0;
  try
    {
      Widget w;
      Other o;
      Manual m (1);
      early_exit ();
      if (w.x != 7 || o.x != 7 || m.x != 1)
	return 3;
    }
  catch throws (std::error)
    {
      return 2;
    }
  g_code = 11;
  try
    {
      Widget w;
      return 4;
    }
  catch throws (std::error e)
    {
      return e.code == 11 ? 0 : 5;
    }
}
