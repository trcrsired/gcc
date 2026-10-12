// { dg-additional-options {-std=c++23 -fmodules-ts -fherbceptions -fno-exceptions} }

import herbceptions_1;

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

/* The herbceptions specs survive CMI serialization: f carries the
   throws channel, g the return_failure{int} channel.  */
int h (int x)
{
  int r = 0;
  try { r = f (x); }
  catch throws (std::error e) { r = (int) e.code; }
  return r;
}

int k (int x)
{
  auto e = catch return_failure (g (x));
  return e.failed ? (int) e.error : e.value;
}
