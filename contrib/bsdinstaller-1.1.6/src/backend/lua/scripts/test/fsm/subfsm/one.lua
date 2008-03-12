-- $Id: one.lua,v 1.4 2004/11/26 00:31:03 cpressey Exp $

return {
    name = "one",
    action = function(fsm)
	print("This is sub-state ONE.")
	return fsm:next()
    end
}
