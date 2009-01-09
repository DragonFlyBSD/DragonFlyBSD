# This is a ld genscripts.sh customizer script

# We need to source the original emulation script!

. "${srcdir}/emulparams/${EMULATION_NAME}.sh"

# We want to sort the .note.ABI-tag section to the front
# so that the kernel will find it in the first page of the file.

INITIAL_READONLY_SECTIONS=".note.ABI-tag	${RELOCATING-0} : { *(.note.ABI-tag) }"
