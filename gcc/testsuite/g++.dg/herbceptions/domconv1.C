// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-fails-to-std-error: try() on a
// return_failure{E} call inside a throws function converts E to
// std::error through std::error_domain<E>; catch throws on the call
// gets the converted error.
namespace std {
struct error { const void *domain; __UINTPTR_TYPE__ code; };
enum class my_errc : int { bad = 7 };
template <class T> class error_domain;
template <> class error_domain<my_errc> {
public:
  static const void *domain () noexcept { return (const void *) 0x44; }
  static __UINTPTR_TYPE__ code (my_errc e) noexcept { return (__UINTPTR_TYPE__) e; }
};
}

long long api_i64 () return_failure{std::my_errc}
{
  return_failure std::my_errc::bad;
}
int api_i32 () return_failure{std::my_errc}
{
  return_failure std::my_errc::bad;
}

long long h_i64 () throws { return try (api_i64 ()); }
int h_i32 () throws { return try (api_i32 ()); }

int
main ()
{
  try
    {
      (void) h_i64 ();
      return 1;
    }
  catch throws (std::error e)
    {
      if (e.domain != (const void *) 0x44 || e.code != 7)
	return 2;
    }
  try
    {
      (void) h_i32 ();
      return 3;
    }
  catch throws (std::error e)
    {
      if (e.code != 7)
	return 4;
    }
  return 0;
}
