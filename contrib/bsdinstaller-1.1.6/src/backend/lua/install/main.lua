-- demo/install/main.lua
-- $Id: main.lua,v 1.4 2005/02/24 23:08:04 cpressey Exp $
-- Main segment of the demonstration fake install program.

require "fsm"

-- these are really more like settings...
-- and as such should go in a config file...
App.state.disk_min = 300	-- minimum space (in MB) req'd for install
App.state.max_waste = 8192	-- maximum # sectors to allow to go to waste
				-- when defining partitions/subpartitions

FSM.auto("welcome")
