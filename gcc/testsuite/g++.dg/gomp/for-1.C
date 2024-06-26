// { dg-do compile }

extern void baz(int);
void foo (int j, int k)
{
  int i;

  /* Valid loops.  */
  #pragma omp for
  for (i = 0; i < 10; i++)
    baz (i);

  #pragma omp for
  for (i = j; i <= 10; i+=4)
    baz (i);

  #pragma omp for
  for (i = j; i > 0; i = i - 1)
    baz (j);

  #pragma omp for
  for (i = j; i >= k; i--)
    baz (i);

  // Malformed parallel loops.
  #pragma omp for
  i = 0;		// { dg-error "loop nest expected" }
  for ( ; i < 10; )
    {
      baz (i);
      i++;
    }

  #pragma omp for
  for (i = 0; ; i--)	// { dg-error "missing controlling predicate" }
    {
      if (i >= 10)
	break;		// { dg-error "break" }
      baz (i);
    }

  #pragma omp for
  for (i = 0; i < 10 && j > 4; i-=3)	// { dg-error "invalid controlling predicate" }
    baz (i);

  #pragma omp for
  for (i = 0; i < 10; i-=3, j+=2)	// { dg-error "invalid increment expression" }
    baz (i);
}
