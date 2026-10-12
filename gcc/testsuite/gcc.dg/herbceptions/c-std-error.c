/* { dg-do compile } */
/* { dg-options "-fherbceptions -std=c2x" } */
/* Port of clang's herbceptions-cxx-std-error-builtin.c: the
   compiler-fabricated __cxx_std_error is available in C too, and is
   what extern "C" functions use to participate in the error channel.  */

#ifndef __CXX_STD_ERROR_TYPE__
#error "expected __CXX_STD_ERROR_TYPE__ to be defined under -fherbceptions"
#endif

typedef __CXX_STD_ERROR_TYPE__ c_error;
_Static_assert (sizeof (c_error) == 2 * sizeof (void *),
		"cxx_std_error is the two-word {domain, code} ABI");

extern __cxx_std_error c_produce (void);
extern void c_consume (__cxx_std_error) return_failure{__cxx_std_error};

__cxx_std_error
relay (void) return_failure{__cxx_std_error}
{
  return c_produce ();
}
