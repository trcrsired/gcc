// { dg-do compile }
// { dg-options "-fherbceptions -fno-exceptions -std=c++23" }
// A herbceptions error escaping constant evaluation is a hard
// non-constant diagnostic, not an ICE (and not silently folded).

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

constexpr int tf (int x) throws
{
  if (x < 0)
    throw throws (std::error{nullptr, (__UINTPTR_TYPE__)-x});
  return x;
}

// The unhandled `try' is itself rejected: `bad' has no spec.
constexpr int bad () { return try (tf (-1)); }  // { dg-error "unhandled herbceptions failure" }

int main () {}
