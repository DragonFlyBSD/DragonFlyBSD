-- $Id: 020_second.lua,v 1.4 2004/11/26 04:40:14 cpressey Exp $

return {
    name = "Second Thing",
    short_desc = "This is the second thing",
    effect = function()
	print("This is the 2nd thing.")
	return Menu.CONTINUE
    end
}
