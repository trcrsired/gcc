// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// The failure discriminant must be loaded on the shared return edge,
// after scope-exit cleanups run: a destructor that makes a call on
// the way out may not clobber the channel flag register.
extern "C" int close (int);

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

struct fd {
  int f;
  fd () : f (0) {}
  ~fd () { close (f); }   // call on the return edge clobbers call-clobbered regs
};

struct pair { void *a; void *b; };

pair
inner () throws
{
  fd x;                    // ~fd() runs between the flag store and ret
  return pair{(void *) 0x1000, (void *) 0x2000};
}

int
main ()
{
  try
    {
      pair p = inner ();
      return p.a == (void *) 0x1000 ? 0 : 3;
    }
  catch throws (std::error)
    {
      return 7;
    }
}
