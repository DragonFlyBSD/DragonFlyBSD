-- $Id: 010_first.lua,v 1.4 2004/11/26 04:40:14 cpressey Exp $

return {
    name = "First Thing",
    short_desc = "This is the first thing",
    effect = function()
	print("This is the 1st thing.")
	return Menu.CONTINUE
    end
}
