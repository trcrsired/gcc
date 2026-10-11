// { dg-do compile { target c++17 } }
// { dg-additional-options "-fherbceptions" }

// Dependent throws(expr) specs.

template<bool B> void h() throws(B) {}  // { dg-final { scan-assembler "_Z1hILb1EEDgT_Evv" } }
template void h<true>();
template void h<false>();

template<bool B> struct S {
  int x() throws(B);
};
template<bool B> int S<B>::x() throws(B) { return 1; }
template struct S<true>;
template struct S<false>;
