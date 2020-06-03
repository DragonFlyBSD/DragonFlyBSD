// Transform Linux kmalloc and krealloc calls by DragonFly-compatible versions
// Usage: spatch --no-includes --sp-file scripts/linux_kmalloc.cocci --in-place <files>

@@
expression E1, E2, E3, E4;
@@

(
- E1 = kmalloc(E2, E3);
+ E1 = kmalloc(E2, M_DRM, E3);
|
- E1 = krealloc(E2, E3, E4);
+ E1 = krealloc(E2, E3, M_DRM, E4);
)
