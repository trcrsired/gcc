// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-try-default-arg: a throws call in a default
// argument is evaluated in the *caller's* context — routes to the
// catch throws handler in a try scope, auto-propagates in a throws
// caller.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

bool g_fail;

bool
daylight () throws
{
  if (g_fail)
    throw throws (std::error{(const void *) 0x9, 42});
  return true;
}

template <long X>
int
local (int ts, bool dl = daylight ()) throws
{
  return ts;
}

int
outer () throws
{
  return local<0> (0);	// default arg propagates through this channel
}

int
main ()
{
  g_fail = false;
  try
    {
      int r = local<0> (0);
      if (r != 0)
	return 3;
    }
  catch throws (std::error)
    {
      return 2;
    }
  g_fail = true;
  try
    {
      (void) local<0> (0);
      return 4;
    }
  catch throws (std::error e)
    {
      if (e.code != 42)
	return 5;
    }
  try
    {
      (void) outer ();
      return 6;
    }
  catch throws (std::error e)
    {
      return e.code == 42 ? 0 : 7;
    }
}
