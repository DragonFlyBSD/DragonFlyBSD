#!/usr/bin/env gdb -batch -x

set $start = (struct mod_metadata **)&__start_set_modmetadata_set
set $end = (struct mod_metadata **)&__stop_set_modmetadata_set

set $p = $start - 1
while $p + 1 < $end
	set $p = $p + 1
	set $d = *$p

	if $d->md_type == 2
		printf "module %s\n", $d->md_cval
	end
	if $d->md_type == 1
		set $dp = (struct mod_depend *)$d->md_data

		printf "depend %s %d %d %d\n", $d->md_cval, \
			$dp->md_ver_minimum, \
			$dp->md_ver_preferred, \
			$dp->md_ver_maximum
	end
	if $d->md_type == 3
		set $dv = (struct mod_version *)$d->md_data

		printf "version %s %d\n", $d->md_cval, $dv->mv_version
	end
end
