-- $Id: middle.lua,v 1.5 2004/11/26 00:31:03 cpressey Exp $

return {
    name = "middle",
    action = function(fsm)
	print("And this is the middle...")
	return "sub-fsm"
    end
}
