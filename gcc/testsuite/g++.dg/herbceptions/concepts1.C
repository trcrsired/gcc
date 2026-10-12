// { dg-do compile { target c++20 } }
// { dg-options "-fherbceptions -fno-exceptions" }
// `{ expr } throws' in compound requirements — GCC port of clang's
// herbceptions-concepts.cpp semantics:
//   {f()} throws    == the resolved callee carries an error channel
//   {f()} noexcept  == the callee cannot fail at all (a `throws' fn
//                     does NOT satisfy it; throws(false) does)

struct bar {};

constexpr void foo_throws_false (bar) throws(false) {}
constexpr void foo_throws_true (bar) throws(true) {}
constexpr void foo_throws (bar) throws {}
constexpr void foo_rf (bar) return_failure{int} {}
constexpr void foo_noexcept (bar) noexcept {}
void foo_plain (bar) {}

template<typename T>
concept is_throws_false = requires (T t)
{
    { foo_throws_false (t) } throws;
};

template<typename T>
concept is_throws_true = requires (T t)
{
    { foo_throws_true (t) } throws;
};

template<typename T>
concept is_throws = requires (T t)
{
    { foo_throws (t) } throws;
};

template<typename T>
concept is_rf = requires (T t)
{
    { foo_rf (t) } throws;
};

template<typename T>
concept is_noexcept_false = requires (T t)
{
    { foo_throws_false (t) } noexcept;
};

template<typename T>
concept is_noexcept_true = requires (T t)
{
    { foo_throws_true (t) } noexcept;
};

template<typename T>
concept is_noexcept_rf = requires (T t)
{
    { foo_rf (t) } noexcept;
};

template<typename T>
concept is_noexcept_plain = requires (T t)
{
    { foo_noexcept (t) } noexcept;
};

template<typename T>
concept is_throws_plain = requires (T t)
{
    { foo_plain (t) } throws;
};

/* throws(false) cannot fail: {f()} throws is false */
static_assert (!is_throws_false<bar>);

/* throws(true)/bare throws can fail through the throws channel */
static_assert (is_throws_true<bar>);
static_assert (is_throws<bar>);

/* `{f()} throws' names the implicit throws channel only — a
   return_failure{E} callee carries a different channel and does
   not satisfy it (matches clang).  */
static_assert (!is_rf<bar>);

/* A plain function cannot fail through the channel */
static_assert (!is_throws_plain<bar>);

/* throws(false) is noexcept */
static_assert (is_noexcept_false<bar>);

/* throws(true)/return_failure are NOT noexcept — GCC rejects any
   spec'd callee here (clang's requirement check only sees the
   throws spec, which contradicts its own noexcept() trait that
   also reports false for return_failure{E} callees).  */
static_assert (!is_noexcept_true<bar>);
static_assert (!is_noexcept_rf<bar>);

/* plain noexcept satisfies noexcept */
static_assert (is_noexcept_plain<bar>);

/* noexcept(expr) trait query */
static_assert (!noexcept (foo_throws_true (bar ())));
static_assert (noexcept (foo_throws_false (bar ())));
static_assert (!noexcept (foo_throws (bar ())));
static_assert (!noexcept (foo_rf (bar ())));
