-- $Id: apptest.lua,v 1.7 2005/02/02 22:26:20 cpressey Exp $

require "app"
require "storage"

App.start({
    name = "Test application",
    logfile = "testapp.log",
    ui = Dfui.new{
	transport = "tcp",
	rendezvous = "9999"
    }
})

print("OS is: " .. App.os.name)

-- Create a storage-descriptor for the application.
App.state.storage = StorageDescriptor.new()
App.state.storage:survey()

print(App.expand("The root is ``${root}'' and the temp dir is ``${tmp}''."))
print(App.expand("And cpdup is ``${CPDUP}''."))
print(App.expand("And boo is ``${BOO}''."))

t1 = App.open_tmpfile("foo.txt", "w")
if t1 then
	t1:write("This goes into foo.txt.")
	t1:close()
else
	App.log_warn("Can't open ${tmp}/foo.txt!")
end

t2, t2_name = App.open_tmpfile()
if t2 then
	t2:write("This goes into some file called " .. t2_name .. ".")
	t2:close()
else
	App.log_warn("Can't open ${tmp}/" .. t2_name .. "!")
end

t1 = App.open_tmpfile("foo.txt", "r")
if t1 then
	for line in t1:lines() do print("foo.txt:", line) end
	t1:close()
end

t2 = App.open_tmpfile(t2_name, "r")
if t2 then
	for line in t2:lines() do print(t2_name .. ":", line) end
	t2:close()
end

App.view_log()

if App.ui:confirm("Confirm dialog test.  Show an inform dialog?") then
	App.ui:inform("Inform dialog test.")
end

App.stop()
