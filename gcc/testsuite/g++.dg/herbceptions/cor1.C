// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// { dg-additional-options "-fcoroutines" }
// Port of LLVM herbceptions-coroutine: a `throws' coroutine routes an
// escaping failure to promise.unhandled_herbception(__cxx_std_error);
// the promise stores the channel error for the caller to observe.
#include <coroutine>
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

/* A non-throws coroutine compiles fine alongside the machinery.  */
Task
plain_task ()
{
  co_return;
}

struct HerbTask {
  struct promise_type {
    __cxx_std_error error{};
    HerbTask get_return_object ()
    {
      return HerbTask{std::coroutine_handle<promise_type>::from_promise (*this)};
    }
    std::suspend_never initial_suspend () { return {}; }
    std::suspend_never final_suspend () noexcept { return {}; }
    void return_void () {}
    void unhandled_herbception (__cxx_std_error e) noexcept { error = e; }
  };
  std::coroutine_handle<promise_type> h;
};

static bool g_fail;

void
some_throws () throws
{
  if (g_fail)
    throw throws (std::error{(const void *) 0x5, 42});
}

HerbTask
herb_task (int x) throws
{
  some_throws ();
  co_return;
}

int
main ()
{
  g_fail = false;
  HerbTask t = herb_task (0);
  if (t.h.promise ().error.code != 0)
    return 1;
  g_fail = true;
  HerbTask t2 = herb_task (0);
  __cxx_std_error e = t2.h.promise ().error;
  return (e.domain == (const void *) 0x5 && e.code == 42) ? 0 : 2;
}
