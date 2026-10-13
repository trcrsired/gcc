// { dg-do run { target { *-*-linux* } } }
// { dg-options "-O2 -fherbceptions -fno-exceptions" }
// A reference-returning spec'd call used as a value rather than
// dereferenced -- `T &r = ++it', a spec'd operator++ -- left the
// try-expr's value as an INDIRECT_REF typed as the referent while the
// expression itself is typed as the reference.  gimplify_modify_expr's
// pointer-conversion block then called fold_convert_loc with a record
// where a reference was expected and aborted (fold-const.cc:2620).  The
// channel slot holds the referent's address, and handing the binding
// that pointer directly is what gimplify accepts.
//
// Regression test for bugs/gcc_herbceptions/refreturn-range-for.

namespace std
{
  struct error
  {
    const void *domain;
    __UINTPTR_TYPE__ code;
  };
}

struct itr
{
  unsigned long a;
  unsigned long b;
  unsigned long c;
};

inline itr &
operator++ (itr &i) throws
{
  i.a = 0x11;
  i.b = 0x22;
  i.c = 3;
  return i;
}

inline itr &
inc (itr &i) throws
{
  i.a = 0x33;
  return i;
}

int
main ()
{
  itr i{};

  itr &r = ++i;
  if (r.a != 0x11 || r.b != 0x22 || r.c != 3)
    return 1;

  /* The same call written without the operator syntax already worked;
     keep it so the two spellings stay covered.  */
  itr j{};
  itr &s = inc (j);
  if (s.a != 0x33)
    return 2;

  return 0;
}
