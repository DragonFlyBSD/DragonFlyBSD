-- $Id: 200_install.lua,v 1.7 2005/02/24 23:08:03 cpressey Exp $

require "gettext"

return {
    name = _("Install %s", App.os.name),
    short_desc = _("Install %s on this computer system", App.os.name),
    effect = function()
	App.descend("install")
	return Menu.CONTINUE
    end
}
