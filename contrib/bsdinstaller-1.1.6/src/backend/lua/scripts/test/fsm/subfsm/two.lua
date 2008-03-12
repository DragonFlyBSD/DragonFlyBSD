-- $Id: two.lua,v 1.4 2004/11/26 00:31:03 cpressey Exp $

return {
    name = "two",
    action = function(fsm)
	print("This is sub-state TWO.")
	return fsm:next()
    end
}
