// 2004-12-03  Paolo Carlini  <pcarlini@suse.de>
//
// Copyright (C) 2004 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// 4.5.2 Composite type traits

#include <tr1/type_traits>
#include <testsuite_hooks.h>
#include <testsuite_tr1.h>

class ClassType { };

void test01()
{
  bool test __attribute__((unused)) = true;
  using std::tr1::is_fundamental;
  using __gnu_test::test_category;
  
  VERIFY( (test_category<is_fundamental, void, true>()) );
  VERIFY( (test_category<is_fundamental, char, true>()) );
  VERIFY( (test_category<is_fundamental, signed char, true>()) );
  VERIFY( (test_category<is_fundamental, unsigned char, true>()) );
#ifdef _GLIBCXX_USE_WCHAR_T
  VERIFY( (test_category<is_fundamental, wchar_t, true>()) );
#endif
  VERIFY( (test_category<is_fundamental, short, true>()) );
  VERIFY( (test_category<is_fundamental, unsigned short, true>()) );
  VERIFY( (test_category<is_fundamental, int, true>()) );
  VERIFY( (test_category<is_fundamental, unsigned int, true>()) );
  VERIFY( (test_category<is_fundamental, long, true>()) );
  VERIFY( (test_category<is_fundamental, unsigned long, true>()) );
  VERIFY( (test_category<is_fundamental, long long, true>()) );
  VERIFY( (test_category<is_fundamental, unsigned long long, true>()) );
  VERIFY( (test_category<is_fundamental, float, true>()) );
  VERIFY( (test_category<is_fundamental, double, true>()) );
  VERIFY( (test_category<is_fundamental, long double, true>()) );

  // Sanity check.
  VERIFY( (test_category<is_fundamental, ClassType, false>()) );
}

int main()
{
  test01();
  return 0;
}
