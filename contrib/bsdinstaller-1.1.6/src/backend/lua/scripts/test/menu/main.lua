-- $Id: main.lua,v 1.7 2005/02/02 22:26:20 cpressey Exp $

require "dfui"
require "app"

App.start()

Menu.auto{
    name = "Automatically Constructed Menu",
    short_desc =
	"This menu was automatically constructed from " ..
	"the Lua script files in the same directory " ..
	"as this Lua script file (scripts/test/menu.)",
    exit_item = {
        name = "Quit",
	short_desc = "Quit this silly example program",
	effect = function()
		return Menu.DONE
	end
    }
}

App.stop()
