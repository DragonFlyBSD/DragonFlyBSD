-- lib/fsm.lua
-- $Id: fsm.lua,v 1.18 2005/03/29 21:04:19 cpressey Exp $
-- Framework for simple Finite State Machines in Lua.

-- BEGIN lib/fsm.lua --

local POSIX = require("posix")
local FileName = require("filename")
local App = require("app")

-- Global "class" variable:
FSM = {}

-- Global "static methods":

-- Create a new FSM object instance.
-- This object can then have states added to it with
-- fsm:register(StateName, Function), and can be run
-- with fsm:run(InitialStateName).
FSM.new = function()
	local fsm = {}
	local state = {}		-- a dictionary of state name -> state
	local sequence = {}		-- an array of state number -> state
	local current_state = nil	-- reference to the current state

	fsm.register = function(fsm, tab)
		state[tab.name] = {
		    name = tab.name,
		    title = tab.title,
		    action = tab.action,
		    position = table.getn(sequence) + 1
		}
		table.insert(sequence, state[tab.name])
	end

	fsm.next = function(fsm)
		return sequence[current_state.position + 1]
	end

	fsm.prev = function(fsm)
		return sequence[current_state.position - 1]
	end

	fsm.current = function(fsm)
		return current_state
	end

	local resolve_state = function(x)
		if type(x) == "string" then
			if state[x] then
				return state[x]
			else
				error("No state named '" .. x ..
				      "' exists in FSM")
			end
		elseif type(x) == "number" then
			if sequence[x] then
				return sequence[x]
			else
				error("No state number " .. tostring(x) ..
				      " exists in FSM")
			end
		elseif x == nil then
			return nil
		elseif type(x) == "table" then
			local i, v
			
			for i, v in state do
				if x == v then
					return x
				end
			end
			error("State object `" .. tostring(x) ..
			      "' does not exist in FSM")
		end
		error("Invalid state reference: " .. tostring(x))
	end

	fsm.run = function(fsm, start_state)
		local result

		current_state = resolve_state(start_state)
		while current_state do
			if type(current_state.action) ~= "function" then
				error("State '" .. current_state.name ..
				    "' does not define an action function")
			else
				result = current_state.action(fsm)
				current_state = resolve_state(result)
			end
		end
	end

	-- Note that we only return the table of functions;
	-- we do not return the state table.  However, a
	-- reference ("upvalue" in Lua terminology) to the
	-- state table is still carried along inside the
	-- fsm function table; in this way it is retained,
	-- and it is also protected from modification from
	-- the outside (i.e. it is encapsulated.)
	return fsm
end

-- Create a new FSM object instance automatically from
-- the Lua script files in the given directory.  Each script
-- should end with a return statement that returns a table
-- describing the state.
FSM.from_dir = function(dir)
	local fsm = FSM.new()
	local file_no, files
	local state, fun
	local state_count = 0

	files = POSIX.dir(dir)
	table.sort(files)

	for file_no in files do
		local full_file = dir .. "/" .. files[file_no]
		local tab

		if files[file_no] ~= FileName.basename(App.current_script) and
		   not FileName.is_dir(full_file) and
		   string.find(files[file_no], "^[^%.].*%.lua$") then
			tab = App.run_script(full_file)
			if tab then
				fsm:register(tab)
				state_count = state_count + 1
			end
		end
	end

	if state_count == 0 then
		error("Directory " .. dir .. " should contain at least one FSM scriptlet")
	end

	return fsm
end

-- Create a new FSM object instance automatically from
-- the Lua script files in the same directory as the Lua
-- script file that invoked this function.
FSM.auto = function(start_state)
	local path = FileName.dirname(App.current_script)
	local fsm = FSM.from_dir(path)

	fsm:run(start_state)
end

-- END of lib/fsm.lua --
