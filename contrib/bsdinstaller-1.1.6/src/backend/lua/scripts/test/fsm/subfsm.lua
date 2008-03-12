-- $Id: subfsm.lua,v 1.5 2005/01/25 05:07:07 cpressey Exp $

require "fsm"

return {
    name = "sub-fsm",
    action = function(fsm)
	App.descend("subfsm")
	return "final"
    end
}
