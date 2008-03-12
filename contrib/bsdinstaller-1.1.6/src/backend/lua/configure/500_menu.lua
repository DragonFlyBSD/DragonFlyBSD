-- $Id: 500_menu.lua,v 1.4 2005/02/24 23:08:03 cpressey Exp $

require "dfui"

return {
    name = "configuration_menu",
    title = "Configuration Menu",
    action = function(fsm)
        App.descend("menu")	
	return fsm:next()
    end
}
