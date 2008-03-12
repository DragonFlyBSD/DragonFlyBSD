-- app.lua
-- $Id: app.lua,v 1.52 2005/04/03 20:59:42 cpressey Exp $
-- Lua-based Application Environment static object.

-- BEGIN app.lua --

module("app")

local POSIX = require("posix")
local FileName = require("filename")
local Pty = require("pty")

--[[-----]]--
--[[ App ]]--
--[[-----]]--

-- Application Environment - roughly equivalent to
-- InstallerContext (or i_fn_args in the C version,) but:
--
-- *  this version is written purely in Lua, and
-- *  this version is not specific to the Installer - it could just as well
--    be used for any application that needs:
--
--   o  user interface facilities (highly abstracted)
--   o  configuration, possibly loaded from config files
--      - locations of directories (root dir, temp dir, etc)
--      - names of system commands
--      - etc
--   o  application-wide options
--   o  application-wide state
--   o  logging
--   o  temporary files
--
-- For simplicity, we consider this to be a singleton or
-- "static object" (with a single global "instance" called App.)

App = {}

--
-- Initialize global stuff.
--

App.init = function()
	App.defaults = {
	    name	= "Unnamed Application",
	    logfile	= "unnamed.log",
	    dir = {
		root	= "/",
		tmp	= "/tmp/"
	    },
	    transport	= "tcp",
	    rendezvous	= "9999"
	}
	
	App.last_log_time = -1
	App.conf_path = ""
	App.current_script = arg[0]
	
	App.config = {}
	App.option = {}
	App.state = {}

	App.add_pkg_path("./lib")
	App.add_pkg_path(FileName.dirname(App.current_script) .. "lib")
	App.add_conf_path("./conf")
	App.add_conf_path(FileName.dirname(App.current_script) .. "conf")

	arg = App.process_cmdline(arg)
end

--
-- Startup and shutdown.
--

App.start = function(opt)
	local k, v

	--
	-- Private function to create a dummy user interface adapter
	-- if the App was started without one.
	--
	local new_dummy_ui = function()
		local method = {}
	
		method.start = function(method)
			App.log("Dummy user interface started")
			return true
		end
	
		method.stop = function(method)
			App.log("Dummy user interface stopped")
			return true
		end
	
		method.present = function(method, tab)
			App.dump_table(tab)
			return {
			    action_id = tab.actions[1].id,
			    datasets = tab.datasets
			}
		end
	
		method.inform = function(method, msg)
			App.log("INFORM: %s", msg)
			return { action_id = "ok", datasets = {} }
		end
		
		method.confirm = function(method, msg)
			App.log("CONFIRM: %s", msg)
			return true
		end
	
		method.select = function(method, msg, map)
			local k, v
			App.log("SELECT: %s", msg)
			for k, v in map do
				return v
			end
		end
	
		method.select_file = function(method, tab)
			App.log("SELECT FILE: %s", tab.title or "Select File")
			return "cancel"
		end
	
		--
		-- Constructor within a constructor, here...
		--
		method.new_progress_bar = function(method, tab)
			local method = {}
	
			method.start = function(method)
				App.log("START PROGRESS BAR")
				return true
			end
	
			method.set_amount = function(method, new_amount)
				App.log("SET PROGRESS AMOUNT: %d", new_amount)
				return true
			end
	
			method.set_short_desc = function(method, new_short_desc)
				App.log("SET PROGRESS DESC: %d", new_short_desc)
				return true
			end
	
			method.update = function(method)
				App.log("PROGRESS UPDATE: %d", new_amount)
				return true
			end
	
			method.stop = function(method)
				App.log("STOP PROGRESS BAR")
				return true
			end
	
			return method
		end
	
		return method
	end

	--
	-- Begin setting up the App.
	--

	-- Set up defaults.
	if not opt then
		opt = {}
	end

	App.merge_tables(opt, App.defaults, function(key, dest_val, src_val)
		if not dest_val then
			return src_val
		else
			return dest_val
		end
	end)

	-- Set name of application.
	App.name = opt.name
	App.log_filename = opt.logfile

	-- Set up directories, and make sure each ends with a slash.
	App.dir = opt.dir
	for k, v in App.dir do
		if string.sub(v, -1) ~= "/" then
			App.dir[k] = v .. "/"
		end
	end

	-- Determine the operating system.
	App.os = {}
	App.os.name = App.determine_os_name()
	-- App.os.version = App.determine_os_version()

	-- Open our logfile.
	App.open_log(App.dir.tmp .. App.log_filename)
	App.log(App.name .. " started")

	-- Load command names, if available.
	App.cmd_names = App.load_conf("cmdnames")

	-- Set up the ${}-expansion function.
	App.expand = function(str, ...)
		local ltables = arg or {}
		local gtables = {App.cmd_names, App.dir}

		local result = string.gsub(str, "%$%{([%w_]+)%}", function(key)
			local i, tab, value

			if table.getn(ltables) > 0 then
				for i, tab in ipairs(ltables) do
					value = tab[key]
					if value then
						return value
					end
				end
			end

			if table.getn(gtables) > 0 then
				for i, tab in ipairs(gtables) do
					value = tab[key]
					if value then
						return value
					end
				end
			end

			App.log_warn("Could not expand `${%s}'", key)
			return "${" .. key .. "}"
		end)

		return result
	end

	-- Set up temporary files.
	App.tmpfile = {}
	
	-- Set up application-specific containers:
	--	config:	application configuration
	--	option:	application-wide options
	--	state:	application-wide state
	App.config = opt.config or App.config
	App.option = opt.option or App.option
	App.state = opt.state or App.state

	-- Seed the random-number generator.
	math.randomseed(os.time())

	-- Set up the App's UI adapter.
	App.ui = opt.ui or new_dummy_ui()
	if not App.ui:start() then
		App.log_fatal("Could not start user interface")
	end
end

App.stop = function()
	App.clean_tmpfiles()
	App.ui:stop()
	App.log("Shutting down")
	App.close_log()
end

App.process_cmdline = function(arg)
	local argn = 1
	local remaining_arg = {}

	while arg[argn] do
		if arg[argn] == "-C" then
			argn = argn + 1
			App.add_conf_path(arg[argn])
		elseif arg[argn] == "-L" then
			argn = argn + 1
			App.add_pkg_path(arg[argn])
		elseif arg[argn] == "-R" then
			argn = argn + 1
			local script_name = App.find_script(arg[argn]) or arg[argn]
			local ok, result = App.run(script_name)
			if not ok then
				io.stderr:write("warning: could not run `" ..
				    tostring(script_name) .. "':\n")
				io.stderr:write(result .. "\n")
			end
		elseif string.find(arg[argn], "=") then
			App.set_property(arg[argn])
		else
			table.insert(remaining_arg, arg[argn])
		end

		argn = argn + 1
	end

	return remaining_arg
end

--
-- Given a string in the form "foo.bar=baz", set the member "bar" of the
-- subtable "foo" of the App object to "baz".
--
App.set_property = function(expr)
	local found, len, k, v, c, r, i, t

	t = App.defaults
	r = {}
	found, len, k, v = string.find(expr, "^(.*)=(.*)$")
	for c in string.gfind(k, "[^%.]+") do
		table.insert(r, c)
	end
	for i, c in r do
		if i == table.getn(r) then
			t[c] = v
		else
			if not t[c] then
				t[c] = {}
			end
			if type(t[c]) == "table" then
				t = t[c]
			else
				App.log_warn("%s: not a table", tostring(c))
			end
		end
	end
end

--
-- Add a directory to package.path (used by compat-5.1.)
--
App.add_pkg_path = function(dir)
	if package and package.path then
		if package.path ~= "" then
			package.path = package.path .. ";"
		end
		package.path = package.path .. tostring(dir) .. "/?.lua"
	end
end

--
-- Add a directory to App.conf_path (used by App.load_conf().)
--
App.add_conf_path = function(dir)
	if App.conf_path ~= "" then
		App.conf_path = App.conf_path .. ";"
	end
	App.conf_path = App.conf_path .. tostring(dir) .. "/?.lua"
end

--
-- Run a Lua script.
-- Note that the script name must be either relative to the
-- current working directory, or fully-qualified.
-- If relative to the current script, use App.find_script first.
-- This function returns two values:
--    the first is the success code, either true or false
--    if true, the second is the result of the script
--    if false, the second is an error message string.
--
App.run = function(script_name, ...)
	local save_script = App.current_script
	local save_args = ARG
	local ok, result, fun, errmsg

	if App.option.fatal_errors then
		assert(script_name and type(script_name) == "string",
		       "bad filename " .. tostring(script_name))
	end
	if not script_name or type(script_name) ~= "string" then
		return false, "bad filename " .. tostring(script_name)
	end

	App.add_pkg_path(FileName.dirname(script_name) .. "lib")
	App.add_conf_path(FileName.dirname(script_name) .. "conf")

	fun, errmsg = loadfile(script_name)

	if App.option.fatal_errors then
		assert(fun, errmsg)
	end
	if not fun then
		return false, errmsg
	end

	App.current_script = script_name
	ARG = arg
	if App.option.fatal_errors then
		ok = true
		result = fun()
	else
		ok, result = pcall(fun)
	end
	ARG = save_args
	App.current_script = save_script

	return ok, result
end

--
-- Find a Lua script.
--
App.find_script = function(script_name)
	script_name = FileName.dirname(App.current_script) .. script_name

	if FileName.is_dir(script_name) then
		if string.sub(script_name, -1, -1) ~= "/" then
			script_name = script_name .. "/"
		end
		return script_name .. "main.lua"
	elseif FileName.is_file(script_name) then
		--
		-- Just execute that script.
		--
		return script_name
	else
		--
		-- Couldn't find it relative to the current script.
		--
		io.stderr:write("WARNING: could not find `" .. script_name .. "'\n")
		return nil
	end
end

--
-- Dump the contents of the given table to stdout,
-- primarily intended for debugging.
--
App.dump_table = function(tab, indent)
	local k, v

	if not indent then
		indent = ""
	end

	for k, v in tab do
		if type(v) == "table" then
			print(indent .. tostring(k) .. "=")
			App.dump_table(v, indent .. "\t")
		else
			print(indent .. tostring(k) .. "=" .. tostring(v))
		end
	end
end

--
-- Merge two tables by looking at each item from the second (src)
-- table and putting a value into the first (dest) table based on
-- the result of a provided callback function which receives the
-- key and bother values, and returns the resulting value.
--
-- An 'overriding' merge can be accomplished with:
--	function(key, dest_val, src_val)
--		return src_val
--	end
--
-- A 'non-overriding' merge can be accomplished with:
--	function(key, dest_val, src_val)
--		if dest_val == nil then
--			return src_val
--		else
--			return dest_val
--		end
--	end
--
App.merge_tables = function(dest, src, fun)
	local k, v

	for k, v in src do
		if type(v) == "table" then
			if not dest[k] then
				dest[k] = {}
			end
			if type(dest[k]) == "table" then
				App.merge_tables(dest[k], v, fun)
			end
		else
			dest[k] = fun(k, dest[k], v)
		end
	end
end

--
-- Run a script.  Expects the full filename (will not search.)
-- Displays a nice dialog box if the script contained errors.
--
App.run_script = function(script_name, ...)
	local ok, result = App.run(script_name, unpack(arg))
	if ok then
		return result
	end
	App.log_warn("Error occurred while loading script `" ..
		      tostring(script_name) .. "': " .. tostring(result))
	if App.ui then
		App.ui:present{
		    id = "script_error",
		    name = "Error Loading Script",
		    short_desc = 
			"An internal Lua error occurred while " ..
			"trying to run the script " ..
			tostring(script_name) .. ":\n\n" ..
			tostring(result),
		    role = "alert",
		    actions = {
		        {
			    id = "ok",
			    name = "OK"
			}
		    }
		}
	end
	return nil
end

--
-- Run a sub-application (a script relative to the current script.)
--
App.descend = function(script_name, ...)
	return App.run_script(App.find_script(script_name), unpack(arg))
end

--
-- Wait for a condition to come true.
-- Display a (cancellable) progress bar while we wait.
-- Returns two values: whether the condition eventually
-- did come true, and roughly how long it took (if it
-- timed out, this value will be greater than the timeout.)
--
App.wait_for = function(tab)
	local predicate = tab.predicate
	local timeout = tab.timeout or 30
	local frequency = tab.frequency or 2
	local title = tab.title or "Please wait..."
	local short_desc = tab.short_desc or title
	local pr
	local time_elapsed = 0
	local cancelled = false

	assert(type(predicate) == "function")

	if predicate() then
		return true
	end

	pr = App.ui:new_progress_bar{
	    title = title,
	    short_desc = short_desc
	}
	pr:start()
	
	while time_elapsed < timeout and not cancelled and not result do
		POSIX.nanosleep(frequency)
		time_elapsed = time_elapsed + frequency
		if predicate() then
			return true, time_elapsed
		end
		pr:set_amount((time_elapsed * 100) / timeout)
		cancelled = not pr:update()
	end

	pr:stop()

	return false, time_elapsed
end

--
-- Configuration file loading.
--

App.locate_conf = function(name)
	local comp

	for comp in string.gfind(App.conf_path, "[^;]+") do
		comp = string.gsub(comp, "?", name)
		if FileName.is_file(comp) then
			return comp
		end
	end
	
	return nil
end

App.load_conf = function(name)
	local filename = App.locate_conf(name)

	if filename ~= nil then
		App.log("Loading configuration file '%s'...", filename)
		return App.run_script(filename)
	else
		App.log_warn("Could not locate configuration file '%s'!", name)
		return nil
	end
end

--
-- Logging.
--

App.open_log = function(filename, mode)
	if App.log_file then
		return
	end
	if not mode then
		mode = "w"
	end
	local fh, err = io.open(filename, mode)
	App.log_file = nil
	if fh then
		App.log_file = fh
	else
		error(err)
	end
end

App.close_log = function()
	if App.log_file then
		App.log_file:close()
		App.log_file = nil
	end
end

App.log = function(str, ...)
	local stamp = math.floor(os.time())
	local line = ""

	local write_log = function(s)
		s = s .. "\n"
		io.stderr:write(s)
		if App.log_file then
			App.log_file:write(s)
			App.log_file:flush()
		end
	end

	if stamp > App.last_log_time then
		App.last_log_time = stamp
		write_log("[" .. os.date() .. "]")
	end

	write_log(string.format(str, unpack(arg)))
end

App.log_warn = function(str, ...)
	App.log("WARNING: " .. str, unpack(arg))
end

App.log_fatal = function(str, ...)
	App.log(str, unpack(arg))
	error(str)
end

App.view_log = function()
	local contents = ""
	local fh

	App.close_log()

	fh = io.open(App.dir.tmp .. App.log_filename, "r")
	for line in fh:lines() do
		contents = contents .. line .. "\n"
	end
	fh:close()

	App.ui:present({
		id = "app_log",
		name = App.name .. ": Log",
		short_desc = contents,
		role = "informative",
		minimum_width = "72",
		monospaced = "true",
		actions = {
			{ id = "ok", name = "OK" }
		}
	})
	
	App.open_log(App.dir.tmp .. App.log_filename, "a")
end

--
-- Temporary file handling.
--

App.clean_tmpfiles = function()
	local filename, unused

	for filename, unused in App.tmpfile do
		App.log("Deleting tmpfile: " .. filename)
		os.remove(App.dir.tmp .. filename)
	end
end

-- Registers that the given file (which resides in App.dir.tmp)
-- is a temporary file, and may be deleted when upon exit.
App.register_tmpfile = function(filename)
	App.tmpfile[filename] = 1
end

-- Creates and opens a new temporary file (in App.dir.tmp).
-- If the filename is omitted, one is chosen using the mkstemp
-- system call.  If the mode is omitted, updating ("w+") is
-- assumed.  The file object and the file name are returned.
App.open_tmpfile = function(filename, mode)
	local fh, err

	if not filename then
		fh, filename = POSIX.mkstemp(App.dir.tmp .. "Lua.XXXXXXXX")
		filename = FileName.basename(filename)
	else
		fh, err = io.open(App.dir.tmp .. filename, mode or "w+")
		if err then
			return nil, err
		end
	end
	App.register_tmpfile(filename)
	return fh, filename
end

--
-- Operating system determination.
-- NOTE: this is pretty weak - this is before we have
-- loaded the command locations, and sysctl could be anywhere on path.
-- Besides, this should be overridable somehow on principle.
-- Perhaps even hard-coded.
--

App.determine_os_name = function()
	local pty = Pty.open("sysctl -n kern.ostype")
	local osname = pty:readline()
	pty:close()
	return osname
end

--
-- More debugging.
-- Install logging wrappers around every method in a class/object.
--
App.log_methods = function(obj_method_table)
	local k, v
	for k, v in pairs(obj_method_table) do
		local method_name, orig_fun = k, method[k]
		method[k] = function(...)
			App.log("ENTERING: %s", method_name)
			orig_fun(unpack(arg))
			App.log("EXITED: %s", method_name)
		end
	end
end

return App

-- END of lib/app.lua --
