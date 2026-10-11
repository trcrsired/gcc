// { dg-do compile { target i?86-*-* x86_64-*-* } }
// { dg-options "-fherbceptions" }
// { dg-final { scan-assembler "setc" } }
// { dg-final { scan-assembler "negb" } }
// x86-64 SysV error-channel lowering: the callee fuses its failure
// flag into `negb; ret' and the caller reads CF via `setc'.

namespace std {
struct error {
  const void *domain;
  __UINTPTR_TYPE__ code;
};
} // namespace std

int raise () throws { throw throws (::std::error{}); }
int good () throws { return 7; }
int prop () throws { return raise (); }

int main ()
{
  try {
      prop ();
  } catch throws (::std::error e) {
    return (int)e.code;
  }
  return 0;
}
