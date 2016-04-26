#!/bin/sh

set -e

if [ "$#" -eq 0 ] || [ "$#" -gt 2 ]; then
  echo "Usage: $(basename $0) fw.bin [module_prefix]"
  exit 1
fi

if [ ! -f $1 ]; then
   echo "File $1 does not exist"
   exit 1
fi

if [ "$#" -eq 2 ]; then
MOD_PREFIX=$2
fi

MODULEDIR=${MODULEDIR:-/boot/modules.local}
WORKDIR=$(mktemp -d)
CURDIR=$PWD

FWFILE=$(basename "$1")
# strip .bin and .fw extensions
MODNAME=$(basename $(basename "$1" .bin) .fw)

# if module_prefix is provided, append to module name
if [ "$#" -eq 2 ]; then
MODNAME=${MOD_PREFIX}${MODNAME}
fi

# copy firmware file to workdir
cp -v "$1" "$WORKDIR"/"$FWFILE"

# prefer to use /sys build but provide workaround too
if [ -f /sys/tools/fw_stub.awk ];
then
echo "KMOD=	${MODNAME}" > 			"$WORKDIR"/Makefile
echo "FIRMWS=	${FWFILE}:${MODNAME}" >>	"$WORKDIR"/Makefile
echo ".include <bsd.kmod.mk>" >>		"$WORKDIR"/Makefile

(cd $WORKDIR && make)

else
# workaround case to build a local fw module version w/o kernel sources (should have no impact)
set -x

# ld/objcopy substitutes ' ', '-', '.' and '/' to '_'
FWSYM=$(echo ${FWFILE} |sed 's/ /_/g' | sed 's/-/_/g' |sed 's/\./_/g' |sed 's/\/_//g')
echo FWSYM=${FWSYM}

cd $WORKDIR
ld -b binary --no-warn-mismatch -r -d -o "${FWFILE}.fwo"  "${FWFILE}"

cat << EOF >> ${MODNAME}.c
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/linker.h>
#include <sys/firmware.h>
//#include <sys/systm.h>

extern char _binary_${FWSYM}_start[], _binary_${FWSYM}_end[];

static int
${MODNAME}_fw_modevent(module_t mod, int type, void *unused)
{
  const struct firmware *fp;
  int error;
  switch (type) {
  case MOD_LOAD:
    fp = firmware_register("${MODNAME}", _binary_${FWSYM}_start , (size_t)(_binary_${FWSYM}_end - _binary_${FWSYM}_start), 0, NULL);
    if (fp == NULL)
      goto fail_0;
    return (0);
fail_0:
    return (ENXIO);
  case MOD_UNLOAD:
    error = firmware_unregister("${MODNAME}");
    return (error);
  }
  return (EINVAL);
}

static moduledata_t ${MODNAME}_fw_mod = {
  "${MODNAME}_fw",
  ${MODNAME}_fw_modevent,
  0
};
DECLARE_MODULE(${MODNAME}_fw, ${MODNAME}_fw_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(${MODNAME}_fw, 1);
MODULE_DEPEND(${MODNAME}_fw, firmware, 1, 1, 1);
EOF

FFLAGS="-fno-common -ffreestanding -fno-asynchronous-unwind-tables -fno-omit-frame-pointer -fno-stack-protector"
cc  -O -pipe   -D_KERNEL -Wall -std=c99 -Werror -DKLD_MODULE ${FFLAGS} -mcmodel=kernel -mno-red-zone -c "${MODNAME}.c"
ld  -r -d -o "${MODNAME}.ko" "${FWFILE}.fwo" "${MODNAME}.o"

fi


# copy firmware module to external modules dir
cp -v "$WORKDIR/$MODNAME.ko" "${MODULEDIR}/"

rm -rf "${WORKDIR}"
