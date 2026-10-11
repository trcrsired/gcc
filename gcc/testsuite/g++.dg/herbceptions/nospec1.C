// { dg-do compile { target c++17 } }

// Without -fherbceptions the specifiers are ordinary identifiers.

int throws = 3;
int return_failure = 4;
struct s { int throws; int return_failure; };
int f(int throws) { return throws; }
