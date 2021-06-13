#!/bin/sh

if [ "$1" = "-h" ]; then
	echo "Usage: $0 <count>"
	exit 1
fi

count=${1:-10}
prog="/tmp/calc-vm"

if [ ! -x "${prog}" ]; then
	echo "ERROR: 'calc-vm' not build"
	exit 1
fi

for i in $(seq ${count}); do
	echo "### ${i} ###"
	${prog} 100 ${i}
	echo
done
