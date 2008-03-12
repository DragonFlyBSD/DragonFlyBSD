-- $Id: 010_first.lua,v 1.4 2004/11/26 04:40:14 cpressey Exp $

return {
    name = "First SubThing",
    short_desc = "This is the first subthing",
    effect = function()
	print("This is the 1st subthing.")
	return Menu.CONTINUE
    end
}
