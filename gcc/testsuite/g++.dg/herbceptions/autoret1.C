// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-auto-rethrow: deduced return type on a
// throws function; bare `throw throws;' rethrows the in-flight error.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

unsigned
foo (int x) throws
{
  if (x < 0)
    throw throws (std::error{(const void *) 0x2, (__UINTPTR_TYPE__) -x});
  return x + 1;
}

auto
test (int x) throws
{
  try
    {
      return foo (x);
    }
  catch throws (std::error)
    {
      throw throws;
    }
}

int
main ()
{
  try
    {
      return test (-9) == 10 ? 1 : 3;
    }
  catch throws (std::error e)
    {
      return (e.domain == (const void *) 0x2 && e.code == 9) ? 0 : 2;
    }
}
