/*
 * DragonFly BSD provides the dynamic loader facilities (dlopen/dlfcn/...) in
 * libc itself. However, a number of applications expect these routines to
 * be provided by libdl. We provide a stub libdl with no subroutines to satisfy
 * these applications.
 */

