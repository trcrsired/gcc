// { dg-do compile { target { i?86-*-* x86_64-*-* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2" }
// A void `throws' function that throws from inside a loop has no store to
// the failure slot in the merged return block: the front end stores 1 in
// the throwing block and relies on the entry store of 0 for the normal
// loop exit.  The return-flag folding pass must look through the
// predecessor blocks, not just at a store in the return block, and write
// the flag directly on all three incoming edges (0 from the entry, 1 from
// the throw, 0 from the loop exhaustion).

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

void
f (int x) throws
{
  for (int i = 0; i < x; ++i)
    if (i == 7)
      throw throws std::error{nullptr, 7};
}

// The throw edge and the two success edges.
// { dg-final { scan-assembler "stc" } }
// { dg-final { scan-assembler "clc" } }
// { dg-final { scan-assembler-not "negb" } }
