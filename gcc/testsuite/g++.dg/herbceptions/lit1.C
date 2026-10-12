// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// The fabricated __cxx_std_error record is a literal type — it can
// live in constexpr promises, domain tables, and consteval contexts.

static_assert (__is_literal_type (__cxx_std_error),
	       "__cxx_std_error must be a literal type");

constexpr __cxx_std_error
mk ()
{
  __cxx_std_error e{nullptr, 5};
  return e;
}

struct P { __cxx_std_error err{}; };

constexpr __cxx_std_error e2 = mk ();

int
main ()
{
  return e2.code == 5 ? 0 : 1;
}
