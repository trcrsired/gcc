// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// A spec'd lambda's function-pointer conversion thunk (_FUN) carries
// the same spec: failures in operator() route through the thunk's
// own channel to the caller.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

int
fail () throws
{
  throw throws (std::error{(const void *) 1, 21});
}

int
main ()
{
  auto l = [](int x) throws {
    if (x < 0)
      return fail ();
    return x * 2;
  };
  int (*fp) (int) throws = l;
  try
    {
      if (fp (5) != 10)
	return 2;
    }
  catch throws (std::error)
    {
      return 3;
    }
  try
    {
      fp (-1);
      return 4;
    }
  catch throws (std::error e)
    {
      return (int) e.code;
    }
}
