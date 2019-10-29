//
// Find cases where our macros can be used.
//
// Applies to userland and kernel code.
//
// Order matters in this file because some macros contain other macros.
//

@@
expression A;
expression I;
@@

-A[I/NBBY] |= 1<<(I%NBBY)
+setbit(A, I)

@@
expression A;
expression I;
@@

-A[I/8] |= 1<<(I%8)
+setbit(A, I)

@@
expression A;
expression I;
@@

-A[I/NBBY] &= ~(1<<(I%NBBY))
+clrbit(A, I)

@@
expression A;
expression I;
@@

-A[I/8] &= ~(1<<(I%8))
+clrbit(A, I)

@@
expression A;
expression I;
@@

-(A[I/NBBY] & (1<<(I%NBBY))) == 0
+isclr(A, I)

@@
expression A;
expression I;
@@

-(A[I/8] & (1<<(I%8))) == 0
+isclr(A, I)

@@
expression A;
expression I;
@@

-A[I/NBBY] & (1<<(I%NBBY))
+isset(A, I)

@@
expression A;
expression I;
@@

-A[I/8] & (1<<(I%8))
+isset(A, I)

@@
type E;
E[] T;
@@

- sizeof(T)/sizeof(E)
+ NELEM(T)

@@
type E;
E[] T;
@@

- sizeof(T)/sizeof(*T)
+ NELEM(T)

@@
type E;
E[] T;
@@

- sizeof(T)/sizeof(T[...])
+ NELEM(T)

@@
expression X;
@@

- ((X-1)&X)==0
+ powerof2(X)

@@
expression X;
expression Y;
@@

- ((X+Y-1)/Y)*Y
+ roundup(X, Y)

@@
expression X;
expression Y;
@@

- ((X+(Y-1))/Y)*Y
+ roundup(X, Y)

@@
expression X;
expression Y;
@@

- (X+(Y-1))/Y
+ howmany(X, Y)


@@
expression X;
expression Y;
@@

- (X+Y-1)&(~(Y-1))
+ roundup2(X, Y)

@@
expression X;
expression Y;
@@

- (X+(Y-1))&(~(Y-1))
+ roundup2(X, Y)

@@
expression X;
expression Y;
@@

- X&(~(Y-1))
+ rounddown2(X, Y)

@@
expression X;
expression Y;
@@

- (X/Y)*Y
+ rounddown(X, Y)
