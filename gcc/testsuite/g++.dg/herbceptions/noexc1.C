// { dg-do compile }
// { dg-options "-fherbceptions -fno-exceptions" }
// noexcept spec'd functions report nothrow=false (the herbceptions
// channel is not noexcept), and the spec survives on the type.

int f () throws;
int g () return_failure{int};
int h () noexcept;

static_assert (!noexcept (f ()), "throws is not noexcept");
static_assert (!noexcept (g ()), "return_failure is not noexcept");
static_assert (noexcept (h ()), "");

// noexcept can't combine with a spec.
void bad () noexcept throws;	// { dg-error "" }
