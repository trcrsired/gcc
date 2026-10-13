// { dg-do compile { target c++17 } }
// { dg-options "-fherbceptions -fno-exceptions -Werror=subobject-linkage" }
// The compiler-owned __cxx_std_error ABI record (the two-word {domain, code}
// pair named by std::cxx_std_error) must have external linkage like any other
// namespace-scope class.  When its TYPE_DECL was built by hand in
// cp/decl.cc without TREE_PUBLIC, cp/decl2.cc:min_vis_r saw the type as
// VISIBILITY_ANON and every class holding a std::cxx_std_error field (or a
// function pointer taking one) was diagnosed by -Wsubobject-linkage.  Using
// -Werror makes a regression a compile failure.

struct Outer { __cxx_std_error e; };
struct WithFn { void (*f) (__cxx_std_error) noexcept; };

Outer o;
WithFn w;
