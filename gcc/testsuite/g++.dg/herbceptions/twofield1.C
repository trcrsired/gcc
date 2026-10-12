// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-two-field-struct: a plain two-field
// struct return is not a channel — no discriminant routing.
using size_t = __SIZE_TYPE__;
struct two_field_status { size_t position; size_t in_scatter; };

two_field_status
two_field_fun ()
{
  return {5, 9};
}

int
main ()
{
  two_field_status s = two_field_fun ();
  return (s.position == 5 && s.in_scatter == 9) ? 0 : 1;
}
