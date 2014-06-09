//
// Look for missing lock releases before returning from an error path.
//
// Target: DragonFly
// Copyright:  2012 - LIP6/INRIA
// License:  Licensed under ISC. See LICENSE or http://www.isc.org/software/license
// Author: Julia Lawall <Julia.Lawall@lip6.fr>
//         (original code, adapted for DragonFly by swildner)
// URL: http://coccinelle.lip6.fr/ 
// URL: http://coccinellery.org/ 
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
@rcu_crit_enter exists@
position p1;
@@

crit_enter@p1();
...
crit_exit();

@exists@
position rcu_crit_enter.p1;
@@

*crit_enter@p1();
... when != crit_exit();
?*return ...;

// get_mplock() / rel_mplock()
//
@rcu_get_mplock exists@
position p1;
@@

get_mplock@p1();
...
rel_mplock();

@exists@
position rcu_get_mplock.p1;
@@

*get_mplock@p1();
... when != rel_mplock();
?*return ...;

// lockmgr(..., {LK_EXCLUSIVE,LK_SHARED}) / lockmgr(..., LK_RELEASE)
//
@rcu_lockmgr exists@
position p1;
expression E;
@@

lockmgr@p1(E,\(LK_SHARED\|LK_EXCLUSIVE\));
...
lockmgr(E,LK_RELEASE);

@exists@
position rcu_lockmgr.p1;
expression E;
@@

*lockmgr@p1(E,\(LK_SHARED\|LK_EXCLUSIVE\));
... when != lockmgr(E,LK_RELEASE);
?*return ...;

// lwkt_gettoken(...) / lwkt_reltoken(...)
//
@rcu_lwkt_gettoken exists@
position p1;
expression E;
@@

lwkt_gettoken@p1(E);
...
lwkt_reltoken(E);

@exists@
position rcu_lwkt_gettoken.p1;
expression E;
@@

*lwkt_gettoken@p1(E);
... when != lwkt_reltoken(E);
?*return ...;

// lwkt_serialize_enter(...) / lwkt_serialize_exit(...)
//
@rcu_lwkt_serialize_enter exists@
position p1;
expression E;
@@

lwkt_serialize_enter@p1(E);
...
lwkt_serialize_exit(E);

@exists@
position rcu_lwkt_serialize_enter.p1;
expression E;
@@

*lwkt_serialize_enter@p1(E);
... when != lwkt_serialize_exit(E);
?*return ...;

// spin_lock(...) / spin_unlock(...)
//
@rcu_spin_lock exists@
position p1;
expression E;
@@

spin_lock@p1(E);
...
spin_unlock(E);

@exists@
position rcu_spin_lock.p1;
expression E;
@@

*spin_lock@p1(E);
... when != spin_unlock(E);
?*return ...;

// vm_object_hold(...) / vm_object_drop(...)
//
@rcu_vm_object_hold exists@
position p1;
expression E;
@@

vm_object_hold@p1(E);
...
vm_object_drop(E);

@exists@
position rcu_vm_object_hold.p1;
expression E;
@@

*vm_object_hold@p1(E);
... when != vm_object_drop(E);
?*return ...;

// vn_lock(...) / vn_unlock(...)
//
@rcu_vn_lock exists@
position p1;
expression E;
@@

(
vn_lock@p1(E,...);
|
vget@p1(E);
)
...
(
vn_unlock(E);
|
vput(E);
)

@exists@
position rcu_vn_lock.p1;
expression E;
@@

(
*vn_lock@p1(E,...);
|
*vget@p1(E);
)
... when != \(vn_unlock\|vput\)(E);
?*return ...;

// wlan_serialize_enter(...) / wlan_serialize_exit(...)
//
@rcu_wlan_serialize_enter exists@
position p1;
@@

wlan_serialize_enter@p1();
...
wlan_serialize_exit();

@exists@
position rcu_wlan_serialize_enter.p1;
@@

*wlan_serialize_enter@p1();
... when != wlan_serialize_exit();
?*return ...;
