// { dg-do compile }
// { dg-options "-fherbceptions -fno-exceptions -std=c++23" }
// Constexpr `throw throws' / `try (expr)' / `catch throws': the
// implicit std::error channel folds through constant evaluation,
// including propagation through a spec'd caller and rethrow.

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

constexpr int tf (int x) throws
{
  if (x < 0)
    throw throws (std::error{nullptr, (__UINTPTR_TYPE__)-x});
  return x * 2;
}

constexpr int tg (int x)
{
  int r = 0;
  try { r = tf (x); }
  catch throws (std::error e) { r = (int) e.code; }
  return r;
}
static_assert (tg (5) == 10);
static_assert (tg (-3) == 3);

/* `try (expr)' propagates through a spec'd caller's channel.  */
constexpr int tp (int x) throws { return try (tf (x)); }
constexpr int th (int x)
{
  int r = 0;
  try { r = tp (x); }
  catch throws (std::error e) { r = (int) e.code; }
  return r;
}
static_assert (th (5) == 10);
static_assert (th (-3) == 3);

/* A handler can rethrow the caught error with `throw throws'.  */
constexpr int tr (int x) throws
{
  try { return try (tf (x)); }
  catch throws (std::error e)
    {
      if (e.code == 7)
	return 99;
      throw throws;
    }
}
constexpr int thr (int x)
{
  int r = 0;
  try { r = tr (x); }
  catch throws (std::error e) { r = (int) e.code; }
  return r;
}
static_assert (thr (5) == 10);
static_assert (thr (-7) == 99);
static_assert (thr (-3) == 3);
