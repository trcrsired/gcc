// { dg-do compile { target c++17 } }
// { dg-additional-options "-fherbceptions" }

void f() throws {}                     // { dg-final { scan-assembler "_Z1fDrv" } }
void g() return_failure{int} {}        // { dg-final { scan-assembler "_Z1gDEiEv" } }
void h() noexcept {}                   // { dg-final { scan-assembler "_Z1hv" } }

using A = void() throws;
void use_a(A *pa) {}                   // { dg-final { scan-assembler "_Z5use_aPDrFvvE" } }
using B = int(int) return_failure{int};
void use_b(B *pb) {}                   // { dg-final { scan-assembler "_Z5use_bPDEiEFiiE" } }
