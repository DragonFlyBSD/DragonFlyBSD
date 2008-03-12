-- pipe.lua
-- $Id: pipe.lua,v 1.4 2005/02/06 05:44:13 cpressey Exp $
-- Test of the libaura pipe binding.

f = io.open("/tmp/foo.lua", "w")
f:write([[
io.stdout:write("hello\n")
a = io.stdin:read("*l")
io.stdout:write("you said: <" .. a .. ">\n")
]])
f:close()

p = Pipe.open("lua /tmp/foo.lua")
p:write("no\n")
p:flush()

line, err = p:readline()
while line do
	print(line)
	line, err = p:readline()
end

reason = "UNKNOWN?!?"
if (err == Pipe.EOF) then reason = "end of file" end
if (err == Pipe.TIMEOUT) then reason = "timeout" end
if (err == Pipe.SELECT_ERR) then reason = "error from select()" end
if (err == Pipe.FGETS_ERR) then reason = "error from fgets()" end

print("Stopped, reason: " .. reason)

pstat = p:close()

print("Status from p:close()=" .. pstat)

os.execute("rm /tmp/foo.lua")
