// { dg-do compile { target c++17 } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-template-try: a function-try-block with a
// catch throws handler inside a function template must not emit
// "cannot use 'try' with exceptions disabled" under -fno-exceptions.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

template <typename T>
void foo (T val) try
{
  (void) val;
}
catch throws (::std::error)
{
}

void
test ()
{
  foo<int> (42);
}
