// { dg-additional-options {-std=c++23 -fmodules-ts -fherbceptions -fno-exceptions} }

export module herbceptions_1;
// { dg-module-cmi herbceptions_1 }

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

export int f (int x) throws
{
  if (x < 0)
    throw throws (std::error{nullptr, (__UINTPTR_TYPE__)-x});
  return x * 2;
}

export int g (int x) return_failure{int}
{
  if (x < 0)
    return_failure -x;
  return x;
}
