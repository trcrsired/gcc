// { dg-do run { target { lto && *-*-linux* } } }
// { dg-options "-O2 -flto -fherbceptions -fno-exceptions" }
// LTO variant of noreturn1.C.  A `throws' function hands its result back
// through the deterministic error channel, so neither an explicit nor an
// inferred noreturn may drop the caller's post-call channel test.
//
// This only showed up under -flto, because pass_ipa_free_lang_data runs
// before pass_fixup_cfg and free_lang_data_in_type unconditionally clears
// TYPE_LANG_SLOT_1 of every FUNCTION_TYPE -- which is exactly where the
// C++ front end stores the herbceptions spec.  The spec was therefore
// gone by the time flags_from_decl_or_type looked at it, the call picked
// up ECF_NORETURN from the still-set TREE_THIS_VOLATILE, and
// fixup_noreturn_call / cleanup_tree_cfg deleted the caller's failure
// test (and everything after it) as dead code, so the throw escaped and
// execution ran into garbage.
//
// Regression test for bugs/gcc_herbceptions/noreturn-throws-lto.

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
  return 5;
}
