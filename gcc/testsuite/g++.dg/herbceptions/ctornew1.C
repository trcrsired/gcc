// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-throws-ctor-thisreturn: `new' on a spec'd
// constructor propagates the ctor's channel through the throws
// function; an ordinary ctor compiles alongside.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

struct Foo {
  int x;
  Foo (int v) throws
  {
    if (v < 0)
      throw throws (std::error{(const void *) 0x7, 3});
    x = v;
  }
};

Foo *
make_foo () throws
{
  return new Foo (-1);
}

int
main ()
{
  try
    {
      (void) make_foo ();
      return 1;
    }
  catch throws (std::error e)
    {
      return (e.domain == (const void *) 0x7 && e.code == 3) ? 0 : 2;
    }
}
