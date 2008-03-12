-- gettext.lua
-- $Id: gettext.lua,v 1.5 2005/02/23 20:53:17 cpressey Exp $
-- Lua wrapper functions for Lua 5.0.x gettext binding.

-- BEGIN gettext.lua --

GetText = require("lgettext")

function _(str, ...)
	return string.format(GetText.translate(str), unpack(arg))
end

return GetText

-- END of gettext.lua --
