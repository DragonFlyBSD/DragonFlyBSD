-- $Id: 600_upgrade.lua,v 1.1 2005/03/27 23:19:29 cpressey Exp $

return {
    name = _("Upgrade an Installed System"),
    short_desc = _("Upgrade a system with to the newest available version"),
    effect = function()
	App.descend("upgrade")
	return Menu.CONTINUE
    end
}
