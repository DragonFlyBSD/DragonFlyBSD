-- fsm/main.lua
-- $Id: main.lua,v 1.3 2004/11/22 06:28:53 cpressey Exp $
-- Test of automatically generated FSM's.

require "fsm"

-- automatically create a finite state machine from the
-- files in the same directory as this script, and
-- manually start it:

FSM.auto("start")
