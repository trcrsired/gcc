/* Test for AltiVec function vec_ld, passing a pointer to const vector */
/* { dg-do compile } */
/* { dg-options "-maltivec" } */
/* { dg-require-effective-target powerpc_altivec } */

#include <altivec.h>

typedef vector unsigned char vuc_t;
const vuc_t* p;
vector unsigned char test_vec_ld()
{
        return vec_ld(0,p);
}

