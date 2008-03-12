-- $Id: ConfigFile.lua,v 1.1 2004/11/28 03:52:05 cpressey Exp $

require "app"
require "cmdchain"

--[[------------]]--
--[[ ConfigVars ]]--
--[[------------]]--

ConfigVars = {}
ConfigVars.new = function()
	local cv = {}
	local var = {}

	cv.get = function(cv, name)
		return var[name]
	end

	cv.set = function(cv, name, value)
		var[name] = value
	end

	--
	-- Populate this set of variables from a file.
	--
	-- This isn't perfect.  It doesn't handle variables
	-- with embedded newlines, for example.  It also 
	-- has to execute the script, which is undesirable.
	--
	cv.read = function(cv, filename, filetype)
		local cmds = CmdChain.new()
		local diff, i

		cmds:add(
		   "set | ${root}${SORT} >${tmp}env.before",
		    {
		        cmdline = ". ${filename} && set | ${root}${SORT} >${tmp}env.after",
			replacements = {
			    filename = filename
			}
		    },
		    {
		        cmdline = "${root}${COMM} -1 -3 ${tmp}env.before ${tmp}env.after",
			capture = "comm"
		    },
		    "${root}${RM} -f  ${tmp}env.before ${tmp}env.after"
		)
		
		if not cmds:execute() then
			return false
		end

		diff = cmds:get_output("comm")
		for i in diff do
			local found, ends, k, v

			found, ends, k, v =
			    string.find(diff[i], "^([^=]+)='(.*)'$")
			if found then
				cv:set(k, v)
			else
				found, ends, k, v =
				    string.find(diff[i], "^([^=]+)=(.*)$")
				if found then
					cv:set(k, v)
				end
			end
		end

		return true
	end

	-- Write this set of configuration variable settings to a file.
	cv.write = function(cv, filename, filetype)
		local k, v
		local file = io.open(filename, "a")
		local written = false

		if not file then
			return false
		end

		for k, v in var do
			if not written then
				written = true
				file:write("\n")
				file:write("# -- BEGIN BSD Installer automatically generated configuration  -- #\n")
				file:write("# -- Written on " .. os.date() .. " -- #\n")
			end

			file:write(k .. "='" .. v.. "'\n")
		end

		if written then
			file:write("# -- END of BSD Installer " ..
			    "automatically generated configuration -- #\n")
		end

		file:close()
		
		return true
	end

	return cv
end
