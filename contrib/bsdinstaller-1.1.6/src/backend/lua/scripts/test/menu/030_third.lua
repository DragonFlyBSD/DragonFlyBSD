-- $Id: 030_third.lua,v 1.4 2004/11/26 04:40:14 cpressey Exp $

return {
    name = "Third Thing",
    short_desc = "This is the third thing",
    effect = function()
	print("This is the 3rd thing.")
	return Menu.CONTINUE
    end
}
