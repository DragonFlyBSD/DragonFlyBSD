-- $Id: start.lua,v 1.4 2004/11/26 00:31:03 cpressey Exp $

return {
    name = "start",
    action = function(fsm)
	print("This is the start.")
	return "middle"
    end
}
