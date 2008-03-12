-- $Id: 500_submenu.lua,v 1.5 2005/01/25 05:07:07 cpressey Exp $

require "dfui"

return {
    name = "Enter Submenu",
    short_desc = "Go into an automatically generated submenu",
    effect = function()
	App.descend("500_submenu")
	return Menu.CONTINUE
    end
}
