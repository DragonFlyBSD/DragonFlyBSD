-- $Id: sources.lua,v 1.3 2005/02/24 23:08:03 cpressey Exp $

-- Default configuration file for source objects (files and directories)
-- to copy to the HDD during the install.

-- Note that this conf file should return list of strings - each string
-- is a filename, without any leading root directory specified, which
-- will be copied to the HDD during install.

-- Note that if you (for example) do not want to copy /usr/local/share,
-- you will need to specify all subdirs of /usr/local except for share
-- in this list.

return {
	"COPYRIGHT",
	"bin",
	"boot",
	"cdrom",
	"dev",
	"etc",
	"libexec",
	"lib",
	"kernel",
	"modules",
	"root",
	"sbin",
	"sys",
	"tmp",
	"usr/bin",
	"usr/games",
	"usr/include",
	"usr/lib",
--	"usr/local",	-- we should use mtree to generate the hier for
--	"usr/X11R6",	-- these, then use pkg_add to populate them.
	"usr/libdata",
	"usr/libexec",
	"usr/obj",
	"usr/sbin",
	"usr/share",
	"usr/src",
	"var"
}
