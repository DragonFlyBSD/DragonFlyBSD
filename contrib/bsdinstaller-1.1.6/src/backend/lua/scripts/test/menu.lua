-- test/menu.lua
-- $Id: menu.lua,v 1.5 2004/11/26 04:28:10 cpressey Exp $
-- Test of the DFUI Menu abstraction wrapper thing.

require "dfui"

local c = DFUIConnection.new("tcp", "9999")
c:start()

local done = false

-- manually create a menu,
-- manually populate it with items, and
-- manually present it:

menu = Menu.new(c, {
    name = "My Menu",
    short_desc = "This is my menu."
})

menu:add_item{
    name = "First Thing",
    short_desc = "This is the first thing",
    effect = function()
	print("This is the 1st thing.")
    end
}

menu:add_item{
    name = "Second Thing",
    short_desc = "This is the second thing",
    effect = function()
	print("This is the 2nd thing.")
    end
}

menu:add_item{
    name = "Third Thing",
    short_desc = "This is the third thing",
    effect = function()
	print("This is the 3rd thing.")
    end
}

-- Manually create and add a submenu

submenu = Menu.new(c, {
   name = "My Submenu",
   short_desc = "This is a submenu"
})

submenu:add_item{
    name = "Foo",
    effect = function() print("foo!") end
}
submenu:add_item{
    name = "Bar",
    effect = function() print("bar!") end
}

menu:add_item{
    name = "Submenu",
    short_desc = "This item leads to a submenu",
    effect = function()
        submenu:present()
    end
}

menu:add_item{
    name = "Stop",
    short_desc = "OK, enough of this",
    effect = function()
	done = true
    end
}

while not done do
	menu:present()
end
