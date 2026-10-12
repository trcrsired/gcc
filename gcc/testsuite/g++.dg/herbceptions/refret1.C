// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-throws-ref-return: T&/T&&/T* returns
// through `throws' preserve the underlying value — auto-propagation
// must hand the original pointer through, not copy the referent.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

int g = 42;

int &lref_inner () { return g; }
int &lref_middle () throws { return lref_inner (); }
int &lref_outer () throws { return lref_middle (); }

int &&rref_inner () { return static_cast<int &&> (g); }
int &&rref_middle () throws { return rref_inner (); }
int &&rref_outer () throws { return rref_middle (); }

const int &cref_inner () { return g; }
const int &cref_middle () throws { return cref_inner (); }
const int &cref_outer () throws { return cref_middle (); }

int *ptr_inner () { return &g; }
int *ptr_middle () throws { return ptr_inner (); }
int *ptr_outer () throws { return ptr_middle (); }

int
main ()
{
  int &a = lref_outer ();
  int &&b = rref_outer ();
  const int &c = cref_outer ();
  int *p = ptr_outer ();
  if (&a != &g || &b != &g || &c != &g || p != &g)
    return 1;
  if (a != 42 || b != 42 || c != 42 || *p != 42)
    return 2;
  return 0;
}
