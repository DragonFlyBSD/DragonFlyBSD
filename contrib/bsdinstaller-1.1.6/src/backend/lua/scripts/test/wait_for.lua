-- $Id: wait_for.lua,v 1.1 2005/02/01 22:10:14 cpressey Exp $

require "app"

App.start{
    name = "wait_for test program",
    logfile = "wait_for.log"
}

os.execute("rm -f /tmp/foo")
os.execute("sleep 10 && touch /tmp/foo &")

local ok, t = App.wait_for{
    predicate = function()
	return FileSystem.is_file("/tmp/foo")
    end
}
App.log("App.wait_for() returned %s, %d", tostring(ok), t)

App.stop()
