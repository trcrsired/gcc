// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fexceptions" }
// Legacy-EH boundary conversion (hosted targets): an escaping legacy
// exception converts through the itanium-exception-ptr domain —
// a `throws' function returns the fabricated {domain, code} pair on
// its channel, and `catch throws' catches a legacy throw in the
// same try after typed handlers.  __cxa_error_* are libherbceptions
// runtime entry points; stub them here so the conversion is
// observable.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }
struct myerr { int x; };

extern "C" void *__cxa_error_domain_itanium_exception_ptr () noexcept
{
  return (void *) 0xDEAD;
}
extern "C" __UINTPTR_TYPE__
__cxa_error_code_itanium_exception_ptr (__UINTPTR_TYPE__, void *,
					void *, void *) noexcept
{
  return 77;
}

static bool g_throw;

void
f () throws
{
  if (g_throw)
    throw myerr{};
}

int
rfbad () return_failure{int}
{
  throw myerr{};   // unreachable channel — terminates via the boundary
}

void
legacy ()
{
}

int
main ()
{
  // A throws function's escaping legacy exception becomes std::error.
  g_throw = true;
  try
    {
      f ();
      return 1;
    }
  catch throws (std::error e)
    {
      if (e.code != 77)
	return 2;
    }
  g_throw = false;
  try
    {
      f ();
    }
  catch throws (std::error)
    {
      return 3;
    }

  // `catch throws' catches a legacy throw in the same try.
  int g;
  try
    {
      throw myerr{};
    }
  catch throws (std::error e)
    {
      g = (int) e.code;
    }
  if (g != 77)
    return 4;

  // A typed legacy catch still wins for its own type.
  int h;
  try
    {
      throw 5;
    }
  catch (int v)
    {
      h = v * 10;
    }
  catch throws (std::error e)
    {
      h = (int) e.code;
    }
  return h == 50 ? 0 : 5;
}
