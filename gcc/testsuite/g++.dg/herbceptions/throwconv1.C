// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// A `throw throws E' routed into a `catch throws (std::error)' handler
// fabricates the {domain, code} payload through std::error_domain<E>.
// The handler's std::error class is TREE_ADDRESSABLE -- libherbceptions'
// std::error owns a user-provided destructor and deleted special members
// -- so the fabricated value has to be constructed directly in the
// handler type.  Building a __cxx_std_error constructor and viewing it as
// std::error instead makes gimplification ask create_tmp_var for a
// register temporary of an addressable class, which it refuses to make.
// This is a regression test for that internal compiler error.

namespace std {
struct error {
  const void *domain;
  __UINTPTR_TYPE__ code;
  error () = delete;
  error (error const &) = delete;
  error &operator= (error const &) = delete;
  ~error ();
};
enum class my_errc : int { bad = 7 };
template <class T> class error_domain;
template <> class error_domain<my_errc> {
public:
  static const void *domain () noexcept { return (const void *) 0x44; }
  static __UINTPTR_TYPE__ code (my_errc e) noexcept
  {
    return (__UINTPTR_TYPE__) e;
  }
};
}

std::error::~error () {}

int
main ()
try
{
  throw throws std::my_errc::bad;
}
catch throws (std::error e)
{
  return e.code == 7 ? 0 : 1;
}
