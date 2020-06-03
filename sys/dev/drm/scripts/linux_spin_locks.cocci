// Replace Linux spin_lock/spin_unlock calls by lockmgr ones
// Usage: spatch --no-includes --sp-file scripts/linux_spin_locks.cocci --in-place <files>

@@ expression E1, E2; @@

(
- spin_lock(E1);
+ lockmgr(E1, LK_EXCLUSIVE);
|
- spin_unlock(E1);
+ lockmgr(E1, LK_RELEASE);
|
- spin_trylock(E1);
+ lockmgr_try(E1, LK_EXCLUSIVE);
|
- spin_lock_nested(E1, E2);
+ lockmgr(E1, LK_EXCLUSIVE);
)
