// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2" }
// Behavioural counterpart of flagreturnloop1.C: the loop-exit success
// path must still report success (the flag is only written on the throw
// edge), and the throw edge must still report the error.  Before the
// predecessor dataflow was added the pass simply skipped this shape and
// fell back to the slot reload; both must behave identically.

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

static void
f (int x) throws
{
  for (int i = 0; i < x; ++i)
    if (i == 7)
      throw throws std::error{nullptr, 7};
}

int
main ()
{
  try
    {
      f (7);
    }
  catch throws (std::error e)
    {
      return 1;
    }

  try
    {
      f (100);
      return 2;
    }
  catch throws (std::error e)
    {
      return e.code == 7 ? 0 : 3;
    }
}
