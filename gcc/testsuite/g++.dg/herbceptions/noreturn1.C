// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-noreturn: a throws function that always
// fails still physically returns {payload, i1} — neither an inferred
// nor an explicit noreturn may drop the channel epilogue.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

[[noreturn]] void
die_decl () throws
{
  throw throws (std::error{(const void *) 0x1, 5});
}

__attribute__ ((noreturn)) void
die_gnu () throws
{
  throw throws (std::error{(const void *) 0x1, 7});
}

int
main ()
{
  try
    {
      die_decl ();
      return 1;
    }
  catch throws (std::error e)
    {
      if (e.code != 5)
	return 2;
    }
  try
    {
      die_gnu ();
      return 3;
    }
  catch throws (std::error e)
    {
      return e.code == 7 ? 0 : 4;
    }
}
