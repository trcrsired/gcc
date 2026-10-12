// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// A spec'd constructor propagating a two-pointer aggregate from a
// nested spec'd call must not confuse the value payload with the
// error channel (fast_io posix_file_loader shape).
extern "C" int close (int);

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

struct fd {
  int f;
  fd (int x) : f (x) {}
  ~fd () { close (f); }
};

struct range { char *begin; char *end; };

range
load_impl (int x) throws
{
  return range{(char *) (__UINTPTR_TYPE__) (0x4000 + x),
	       (char *) (__UINTPTR_TYPE__) (0x4000 + x + 8)};
}

range
load (int x) throws
{
  fd pf (x);                       // ~fd() runs on the return edge
  return load_impl (x);
}

struct loader {
  char *begin;
  char *end;
  loader (int x) throws
    {
      range r = load (x);
      begin = r.begin;
      end = r.end;
    }
};

int
main ()
{
  try
    {
      loader l{3};
      return (char *) 0x4003 == l.begin ? 0 : 5;
    }
  catch throws (std::error)
    {
      return 7;
    }
}
