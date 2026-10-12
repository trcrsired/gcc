// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-rethrow-moves-error: `throw throws e'
// inside a catch throws handler builds a fresh channel error from
// the handler's copy; bare `throw throws;' rethrows the in-flight
// error.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

void
foo (int x) throws
{
  if (x < 0)
    throw throws (std::error{(const void *) 0x6, 13});
}

void
copy (int x) throws
{
  try
    {
      foo (x);
    }
  catch throws (std::error e)
    {
      throw throws e;
    }
}

void
bare (int x) throws
{
  try
    {
      foo (x);
    }
  catch throws (std::error e)
    {
      (void) e;
      throw throws;
    }
}

int
main ()
{
  try
    {
      copy (-1);
      return 1;
    }
  catch throws (std::error e)
    {
      if (e.domain != (const void *) 0x6 || e.code != 13)
	return 2;
    }
  try
    {
      bare (-1);
      return 3;
    }
  catch throws (std::error e)
    {
      return (e.domain == (const void *) 0x6 && e.code == 13) ? 0 : 4;
    }
}
