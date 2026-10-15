// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// { dg-additional-options "-fcoroutines" }
// A `throws' coroutine whose return type is not trivially destructible
// is returned in memory -- the C++ front end decides this with
// aggregate_value_p (TREE_ADDRESSABLE), which the target's
// return_in_memory hook need not agree with.  When the ramp is inlined,
// the result must keep the DECL_RESULT path: redirecting it to the
// herbceptions channel (as for a scalar register return) hands back the
// channel contents -- a null task handle at -O1 and above.
//
// On aarch64 -O1+ the ramp's handle came out null, so that
// `fast_io' example 0017 async http-server crashed on Windows-on-ARM.
#include <coroutine>

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

struct task
{
  struct promise_type
  {
    task get_return_object () noexcept
    {
      return task{std::coroutine_handle<promise_type>::from_promise (*this)};
    }
    std::suspend_always initial_suspend () noexcept { return {}; }
    std::suspend_always final_suspend () noexcept { return {}; }
    void return_void () noexcept {}
    void unhandled_exception () noexcept {}
    void unhandled_herbception (__cxx_std_error) noexcept {}
    static void *operator new (std::size_t n, auto &&...) throws
    {
      return ::operator new (n);
    }
    static void operator delete (void *p, std::size_t) noexcept
    {
      ::operator delete (p);
    }
  };
  std::coroutine_handle<promise_type> h;
  ~task () noexcept { if (h) h.destroy (); }
};

static task
co () throws
{
  co_return;
}

int
main ()
{
  auto t = co ();
  return t.h ? 0 : 1;
}
