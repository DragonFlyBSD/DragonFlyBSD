//
// Look for missing lock releases before returning from a function.
//
// Applies to kernel code.
//
// NOTES
// -----
// * The results of running this patch have to be carefully reviewed.
//   Some functions legally return with the lock held, even if the
//   below pattern matches. Some other functions begin with the lock
//   held, only to release and then reacquire it again.
//
// * Consider using -timeout because it might run a long time
//   (indefinitely?) on some files.
//

// crit_enter() / crit_exit()
//
@@
@@
crit_enter();
... when != crit_exit()
    when any
    when strict
(
if (...) { ... when != crit_exit()
+   crit_exit();
    return ...;
}
|
crit_exit();
)

// get_mplock() / rel_mplock()
//
@@
@@
get_mplock();
... when != rel_mplock()
    when any
    when strict
(
if (...) { ... when != rel_mplock()
+   rel_mplock();
    return ...;
}
|
rel_mplock();
)

// lockmgr(..., {LK_EXCLUSIVE,LK_SHARED}) / lockmgr(..., LK_RELEASE)
//
@@
expression l;
@@
lockmgr(l,\(LK_SHARED\|LK_EXCLUSIVE\));
... when != lockmgr(l,LK_RELEASE)
    when any
    when strict
(
if (...) { ... when != lockmgr(l,LK_RELEASE)
+   lockmgr(l,LK_RELEASE);
    return ...;
}
|
lockmgr(l,LK_RELEASE);
)

// lwkt_gettoken(...) / lwkt_reltoken(...)
//
@@
expression l;
@@
lwkt_gettoken(l);
... when != lwkt_reltoken(l)
    when any
    when strict
(
if (...) { ... when != lwkt_reltoken(l)
+   lwkt_reltoken(l);
    return ...;
}
|
lwkt_reltoken(l);
)

// lwkt_serialize_enter(...) / lwkt_serialize_exit(...)
//
@@
expression l;
@@
lwkt_serialize_enter(l);
... when != lwkt_serialize_exit(l)
    when any
    when strict
(
if (...) { ... when != lwkt_serialize_exit(l)
+   lwkt_serialize_exit(l);
    return ...;
}
|
lwkt_serialize_exit(l);
)

// spin_lock(...) / spin_unlock(...)
//
@@
expression l;
@@
spin_lock(l);
... when != spin_unlock(l)
    when any
    when strict
(
if (...) { ... when != spin_unlock(l)
+   spin_unlock(l);
    return ...;
}
|
spin_unlock(l);
)
