-- fsm.lua
-- $Id: fsm.lua,v 1.5 2004/11/26 00:31:03 cpressey Exp $
-- Test of the FSM library.

require("fsm")

-- manually create a finite state machine,
-- manually populate it with states, and
-- manually start it:

fsm = FSM.new()

fsm:register({
    name = "start",
    action = function()
	print("This is the start.")
	return "middle"
    end})

fsm:register({
    name = "middle",
    action = function()
	print("Also, this is the middle...")
	return "end"
    end})

fsm:register({
    name = "end",
    action = function()
	print("And finally, this is the end!")
	return nil
    end})

fsm:run("start")
