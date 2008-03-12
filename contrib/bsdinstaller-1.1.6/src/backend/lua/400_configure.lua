-- $Id: 400_configure.lua,v 1.4 2005/02/24 23:08:03 cpressey Exp $

require "gettext"

return {
    name = _("Configure an Installed System"),
    short_desc = _("Configure a %s OS that has already been installed", App.os.name),
    effect = function()
	App.descend("configure")
	return Menu.CONTINUE
    end
}
