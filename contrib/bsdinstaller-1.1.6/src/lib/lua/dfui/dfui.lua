-- $Id: dfui.lua,v 1.32 2005/04/03 20:46:01 cpressey Exp $
-- Wrapper/helper/extra abstractions for DFUI.

--[[------]]--
--[[ DFUI ]]--
--[[------]]--

--
-- This is a wrapper object around DFUI.Connection and DFUI.Progress,
-- intended to be used as a "UI adapter" for the App object.
--

module("dfui")

DFUI = require "ldfui"
local POSIX = require "posix"

DFUI.log = function(fmt, ...)
	print(string.format(fmt, unpack(arg)))
end

DFUI.new = function(tab)
	local dfui = {}
	local transport = tab.transport or "tcp"
	local rendezvous = tab.rendezvous or "9999"
	local connection

	dfui.start = function(dfui)
		connection = DFUI.Connection.new(transport, rendezvous)
		if connection:start() == 0 then
			connection:stop()
			DFUI.log("Could not establish DFUI connection " ..
			    " on %s:%s", transport, rendezvous)
			return false
		end
		DFUI.log("DFUI connection on %s:%s successfully established",
			transport, rendezvous)
		return true
	end

	dfui.stop = function(dfui)
		return connection:stop()
	end

	dfui.present = function(dfui, tab)
		return connection:present(tab)
	end

	--
	-- Handy dialogs.  (Perhaps a bit too handy?)
	--

	dfui.inform = function(dfui, msg)
		return connection:present({
		    id = "inform",
		    name = "Information",
		    short_desc = msg,
		    role = "informative",
		    actions = {
			{
			    id = "ok",
			    name = "OK"
			}
		    }
		})
	end
	
	dfui.confirm = function(dfui, msg)
		return connection:present({
		    id = "confirm",
		    name = "Are you SURE?",
		    short_desc = msg,
		    role = "alert",
		    actions = {
			{
			    id = "ok",
			    name = "OK"
			},
			{
			    id = "cancel",
			    name = "Cancel"
			}
		    }
		}).action_id == "ok"
	end

	dfui.select = function(dfui, msg, map)
		local action = {}
		local consequence = {}
		local id_num = 0
		local k, v
	
		for k, v in map do
			table.insert(action, {
			    id = tostring(id_num),
			    name = k
			})
			consequence[tostring(id_num)] = v
			id_num = id_num + 1
		end

		return consequence[connection:present({
		    id = "select",
		    name = "Please Select",
		    short_desc = msg,
		    role = "informative",
		    actions = action
		}).action_id]
	end

	dfui.select_file = function(dfui, tab)
		local title = tab.title or "Select File"
		local short_desc = tab.short_desc or title
		local long_desc = tab.long_desc or ""
		local cancel_desc = tab.cancel_desc or "Cancel"
		local dir = assert(tab.dir)
		local ext = tab.ext or nil
		local files, i, filename

		local form = {
		    id = "select_file",
		    name = title,
		    short_desc = short_desc,
		    long_desc = long_desc,
		    
		    role = "menu",

		    actions = {}
		}

		files = POSIX.dir(dir)
		table.sort(files)
		for i, filename in files do
			if not ext or string.find(filename, "%." .. ext .. "$") then
				table.insert(form.actions, {
				    id = filename,
				    name = filename
				})
			end
		end

		table.insert(form.actions, {
		    id = "cancel",
		    name = cancel_desc
		})

		return connection:present(form).action_id
	end

	--
	-- Constructor within a constructor, here...
	--
	dfui.new_progress_bar = function(dfui, tab)
		local method = {}
		local pr
		local title = tab.title or "Working..."
		local short_desc = tab.short_desc or title
		local long_desc = tab.long_desc or ""
		local amount = 0

		pr = DFUI.Progress.new(connection,
		    title, short_desc, long_desc, amount)

		method.start = function(method)
			return pr:start()
		end

		method.set_amount = function(method, new_amount)
			return pr:set_amount(new_amount)
		end

		method.set_short_desc = function(method, new_short_desc)
			return pr:set_short_desc(new_short_desc)
		end

		method.update = function(method)
			return pr:update()
		end

		method.stop = function(method)
			return pr:stop()
		end

		return method
	end

	return dfui
end

return DFUI
