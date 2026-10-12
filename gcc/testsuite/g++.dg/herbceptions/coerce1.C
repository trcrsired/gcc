// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-throws-bitcast-coerce: a register-sized
// two-word struct success shares the channel union with the
// {domain, code} error — the same-width views must not smear.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

struct status { long long p, n; };

int g_mode;

__attribute__ ((noinline)) status
inner () throws
{
  if (g_mode == 1)
    throw throws (std::error{(const void *) 0x8, 99});
  status s;
  s.p = 11;
  s.n = 22;
  return s;
}

__attribute__ ((noinline)) int
outer () throws
{
  for (;;)
    {
      status ret = inner ();
      if (ret.p == 0)
	return (int) ret.n;
      return (int) (ret.p + ret.n);
    }
}

int
main ()
{
  g_mode = 0;
  try
    {
      if (outer () != 33)
	return 2;
    }
  catch throws (std::error)
    {
      return 3;
    }
  g_mode = 1;
  try
    {
      (void) outer ();
      return 4;
    }
  catch throws (std::error e)
    {
      return (e.domain == (const void *) 0x8 && e.code == 99) ? 0 : 5;
    }
}
