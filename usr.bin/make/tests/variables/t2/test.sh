#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/variables/t2/test.sh,v 1.2 2005/03/03 19:13:00 okumoto Exp $

. ../../common.sh

setup_test()
{
	cat > $WORK_BASE/Makefile << "_EOF_"
FILES	= \
		main.c globals.h \
		util.c util.h \
		map.c map.h \
		parser.y lexer.l \
		cmdman.1 format.5
ORANGE	= Variable Variable Variable Variable
APPLE	= AAA BBB CCC
all:
	@echo "Old: ${FILES}"
	@echo "New: ${FILES:S/map/Map/}"
	@echo "New: ${FILES:S/main/&_wrapper/}"

	@echo "${ORANGE:S/Variable/Integer/g}"
	@echo "${ORANGE:S/Variable/Integer/}"

	@echo "${APPLE:S/A/FOO/g}"
	@echo "${APPLE:S/B/BAR/g}"
	@echo "${APPLE:S/C/BAZ/g}"

	@echo "${APPLE:S/^A/FOO/g}"
	@echo "${APPLE:S/^B/BAR/g}"
	@echo "${APPLE:S/^C/BAZ/g}"

	@echo "${APPLE:S/A$/FOO/g}"
	@echo "${APPLE:S/B$/BAR/g}"
	@echo "${APPLE:S/C$/BAZ/g}"
_EOF_
}

desc_test()
{
	echo "Variable expansion with M modifier"
}

eval_cmd $1
