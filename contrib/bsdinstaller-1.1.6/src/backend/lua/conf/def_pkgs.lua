-- $Id: def_pkgs.lua,v 1.2 2005/02/24 23:08:03 cpressey Exp $

-- Default packages to install during the install phase.

-- Note that these packages are specified by Lua regular expressions
-- that will be passed to string.find().  This allows us to specify
-- packages regardless of their version number, etc.

return {
	"^cdrtools-",
	"^cvsup-"
}
