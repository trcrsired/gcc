// { dg-do compile }
// { dg-options "-std=c++26 -fherbceptions -fno-exceptions -O2 -Wuninitialized -Werror=uninitialized" }
// A `throws' function's __hb_result is initialized with .DEFERRED_INIT.
// SSA gimplification splits that into a temporary DEFERRED_INIT call and
// an assignment to the slot, so the payload pass must recognize the
// assignment as the initializer too.  It used to convert the store into
// HERBCEPTION_RETURN_PAYLOAD, which made -Wuninitialized report the
// function's own __hb_result as used uninitialized.  The initializer is
// dead once the local stops being volatile and must simply be dropped.

namespace std
{
  enum class errc { argument_out_of_domain = 33 };
}

void
doSqrt (int *values, int n) return_failure{::std::errc}
{
  for (int i = 0; i < n; ++i)
    {
      if (values[i] < 0)
	return_failure ::std::errc::argument_out_of_domain;
      values[i] = values[i] * 2;
    }
}
