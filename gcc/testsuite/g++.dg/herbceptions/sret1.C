// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-throws-payload/sret: a payload wider than
// the error record returns in memory; the success path must read the
// value storage, not the error view of the channel union.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

struct Big { long long a, b, c; };

Big
big_inline (int n) throws
{
  if (n < 0)
    throw throws (std::error{(const void *) 1, (__UINTPTR_TYPE__) -n});
  return Big{n, n + 1, n + 2};
}

Big
big_via (int n) throws
{
  return big_inline (n);
}

int
main ()
{
  try
    {
      Big b = big_via (1);
      if (b.a != 1 || b.b != 2 || b.c != 3)
	return 2;
    }
  catch throws (std::error)
    {
      return 3;
    }
  try
    {
      Big b = big_via (-5);
      return 4;
    }
  catch throws (std::error e)
    {
      return e.code == 5 ? 0 : 6;
    }
}
