-- $Id: configvars.lua,v 1.1 2004/11/28 03:52:05 cpressey Exp $
-- Test of ConfigVars objects.

require "app"
require "configvars"

App.start()

local fh = io.open("/tmp/test.conf", "w")
fh:write("# comment\n")
fh:write("BOO=Baz\n")
fh:write([[FOO=Silly"'"Quote'"'Madness"'!!!'"]] .. "\n")
fh:close()

local cv = ConfigVars.new()
if not cv:read("/tmp/test.conf") then error("Couldn't read test.conf") end
if not cv:write("/tmp/out.conf") then error("Couldn't write out.conf") end

App.stop()
