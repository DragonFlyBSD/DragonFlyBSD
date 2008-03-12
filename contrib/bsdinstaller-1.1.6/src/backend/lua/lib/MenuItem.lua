-- lib/menu.lua
-- $Id: MenuItem.lua,v 1.8 2005/03/29 21:04:19 cpressey Exp $

-- BEGIN lib/menu.lua --

local POSIX = require("posix")
local FileName = require("filename")
local App = require("app")

--[[----------]]--
--[[ MenuItem ]]--
--[[----------]]--

-- Global "class" variable.
MenuItem = {}

-- Global (but private) data and functions.
local next_id = 0		-- next unique id to create
local new_id = function()	-- Create a new unique identifier.
	next_id = next_id + 1
	return tostring(next_id)
end

-- Constructor.
MenuItem.new = function(tab)
	local method = {}
	local id = tab.id or "menu_action_" .. new_id()
	local name = tab.name or ""
	local short_desc = tab.short_desc
	local long_desc = tab.long_desc
	local effect = tab.effect

	method.to_action = function(item)
		return {
		    id = id,
		    name = name,
		    short_desc = short_desc,
		    long_desc = long_desc,
		    effect = effect
		}
	end

	return method
end

--[[------]]--
--[[ Menu ]]--
--[[------]]--

-- Global "class" variable:
Menu = {}

-- Global (and public) symbolic constants:
Menu.CONTINUE = {}
Menu.DONE = {}

-- Global (but private) state variables in the class:
local menu_stack = {}	-- stack of the most recently displayed menus
local make_exit_item = function()
	local exit_item_name = "Exit"

	if table.getn(menu_stack) > 0 then
		exit_item_name = "Return to " ..
		    menu_stack[table.getn(menu_stack)]:get_name()
	end

	return {
	    name = exit_item_name,
	    effect = function()
		return Menu.DONE
	    end
	}
end

-- Create a new menu object instance.
Menu.new = function(opt)
	local method = {}
	local item = {}
	local menu_id = opt.menu_id or "menu_form_" .. new_id()
	local ui = opt.ui or App.ui
	local name = opt.name or ""
	local exit_item = opt.exit_item or make_exit_item()
	local continue_constraint = opt.continue_constraint

	-- Private functions.
	local map_items_to_actions = function(item)
		local k, v
		local action = {}

		for k, v in item do
			action[k] = item[k]:to_action()
		end

		return action
	end

	-- Methods.
	method.add_item = function(menu, tab)
		-- XXX: if tab is already a MenuItem object,
		-- just add it, else:
		table.insert(item, MenuItem.new(tab))
	end

	method.get_name = function(menu)
		return name
	end

	--
	-- Populate this menu with items derived from the Lua script files
	-- in the given directory.  Each script should end with a return
	-- statement that returns a table describing the menu item.
	--
	method.from_dir = function(menu, dir)
		local i, filename, filenames
	
		filenames = POSIX.dir(dir)
		table.sort(filenames)
	
		for i, filename in filenames do
			local full_file = dir .. "/" .. filename
	
			if filename ~= FileName.basename(App.current_script) and
			   not FileName.is_dir(full_file) and
			   string.find(filename, "^[^%.].*%.lua$") then
				local tab = App.run_script(full_file)
				if tab then
					menu:add_item(tab)
				end
			end
		end

		menu:add_item(exit_item)

		return menu
	end

	--
	-- Present this menu to the user.
	--
	method.present = function(menu)
		table.insert(menu_stack, menu)
		local response = ui:present({
			id = menu_id,
			name = opt.name,
			short_desc = opt.short_desc,
			long_desc = opt.long_desc,
			role = "menu",
			actions = map_items_to_actions(item)
		})
		table.remove(menu_stack)
		return response
	end

	method.loop = function(menu)
		local result = Menu.CONTINUE
	
		while result == Menu.CONTINUE do
			result = menu:present().result
			if continue_constraint then
				result = continue_constraint(result)
			end
		end
	end

	return method
end

-- Create a new menu object instance automatically from
-- the Lua script files in the same directory as the Lua
-- script file that invoked this function.
Menu.auto = function(opt)
	local menu = Menu.new(opt)

	menu:from_dir(FileName.dirname(App.current_script))
	menu:loop()
end

-- END of lib/menu.lua --
