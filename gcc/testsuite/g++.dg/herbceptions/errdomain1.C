// { dg-do compile { target c++17 } }
// { dg-options "-fherbceptions -fno-exceptions -fsyntax-only" }
// Port of clang's herbceptions-error-domain-required.cpp: a
// `return_failure{E}` failure may only reach a channel whose payload type
// differs from E -- the implicit std::error channel of a `throws' function or
// of a `catch throws' handler, or a mismatched `return_failure{E2}` channel --
// through std::error_domain<E>.  Without a visible specialization the raw E
// payload would be bit-copied into the channel slot (a miscompile), so it is
// diagnosed the same way `throw throws E` is.  Regression test for
// bugs/llvm_herbceptions/rf-propagation-through-throws and
// bugs/llvm_herbceptions/catch-throws-on-rf-call.

namespace std {
struct error { const void *domain; __UINTPTR_TYPE__ code; };
enum class ec : unsigned { bad = 7 };
template <class T> class error_domain;
template <> class error_domain<ec> {
public:
  static const void *domain () noexcept;
  static __UINTPTR_TYPE__ code (ec) noexcept;
};
}

int f () return_failure{int};
int fe () return_failure{std::ec};

int auto_in_throws () throws { return f (); }        // { dg-error "no .std::error_domain. specialization" }
int explicit_try_in_throws () throws
{
  return try (f ());                                  // { dg-error "no .std::error_domain. specialization" }
}
int catch_throws_handler ()
{
  try
    {
      return f ();                                    // { dg-error "no .std::error_domain. specialization" }
    }
  catch throws (std::error e)
    {
      return (int) e.code;
    }
}
long mismatch_return_failure () return_failure{long}
{
  return f ();                                        // { dg-error "no .std::error_domain. specialization" }
}

int auto_in_return_failure () return_failure{int} { return f (); }
int explicit_try_in_return_failure () return_failure{int} { return try (f ()); }
int auto_in_throws_with_domain () throws { return fe (); }
int catch_throws_handler_with_domain ()
{
  try
    {
      return fe ();
    }
  catch throws (std::error e)
    {
      return (int) e.code;
    }
}
