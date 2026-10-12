// { dg-do compile }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-canthrow-if-init-condvar: an `if' with a
// C++17 init-statement and a condition variable inside a `throws'
// function must not trip the auto-wrap/can-throw analysis.

struct BoolWrapper
{
  bool value;
  constexpr operator bool () const { return value; }
};

inline constexpr void throwing () throws {}

inline void
outer () throws
{
  auto lam = []() throws
    {
      if (BoolWrapper b{true}; b)
	throwing ();
    };
  lam ();
}
