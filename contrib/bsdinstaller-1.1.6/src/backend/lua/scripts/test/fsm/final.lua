-- $Id: final.lua,v 1.4 2004/11/26 00:31:03 cpressey Exp $

return {
    name = "final",
    action = function(fsm)
	print("Finally, this is the end!")
	return nil
    end
}
