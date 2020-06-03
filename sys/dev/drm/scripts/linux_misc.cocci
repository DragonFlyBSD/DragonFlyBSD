// Replace Linux constant names already defined differently in DragonFly
// Usage: spatch --no-includes --sp-file scripts/linux_misc.cocci --in-place <files>

@@ expression E1; @@

(
- LIST_HEAD(E1);
+ LINUX_LIST_HEAD(E1);
|
- PAGE_MASK
+ LINUX_PAGE_MASK
|
- RB_ROOT
+ LINUX_RB_ROOT
|
- clflush(E1)
+ linux_clflush(E1)
)
