-- test code for Lua<->DFUI binding.
-- $Id: commands.lua,v 1.16 2005/02/18 04:30:49 cpressey Exp $

require "cmdchain"
require "dfui"

App.start{
    name = "Command-execution test program",
    logfile = "cmdtest.log",
    ui = DFUI.new{
	transport = "tcp",
	rendezvous = "9999"
    }
}

-- App.option.fake_execution = true

cc = CmdChain.new()

cc:add(
    { cmdline = "touch /tmp/testing" },
    { cmdline = "sleep 5", desc = "We're waiting five seconds..." },
    { cmdline = "ls -la /tmp/", capture = "ls" }
)

r = cc:preview()
print("cc:preview()=",r)

r = cc:execute()
print("cc:execute()=",r)

r = cc:get_output("ls")
print("cc:get_output(ls)=", r)
for i in r do
	print("-->", r[i])
end

print("*** writing command chain as script to stdout ***")
cc:record(io.stdout)
print("*** command chain script written to stdout ***")

r = CmdChain.new("echo 'hi'", "echo 'there'"):execute()
print("cc:execute()=",r)

App.stop()
