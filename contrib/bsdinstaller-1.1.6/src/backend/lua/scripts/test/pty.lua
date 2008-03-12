-- $Id: pty.lua,v 1.4 2005/04/03 20:28:22 cpressey Exp $

local Pty = require("pty")

p, err = Pty.open("/usr/games/adventure")
print(p, err)

input = {
    "no",
    "go building",
    "get water",
    "get keys",
    "get food",
    "get lantern",
    "go out",
    "go stream",
    "s",
    "quit",
    "yes",
    "ok"
}
i = 1

local read_until_timeout = function(p)
	local acc, text, err

	acc = ""
	text, err = p:read(100)
	while err ~= Pty.TIMEOUT do
		if err then
			return acc, err
		end
		acc = acc .. text
		text, err = p:read(100)
	end

	return acc
end

while true do
	p:write(input[i] .. "\n")
	t, err = read_until_timeout(p)
	if t then
		io.stdout:write(t)
	end
	if err == Pty.EOF then
		break
	end
	if err then
		print("ERROR: ", err)
	end
	i = i + 1
end

p:close()

k = 0
maxi = 100
-- Test for resource leakage.
for i = 1,maxi do
	p, err = Pty.open("ls")
	if p then
		t, err = p:read()
		print(t)
		if p:close() then
			k = k + 1
			--Clock.nanosleep(0.01)
			print("closed #", k)
		end
	else
		print("Cannot open pty: ", err)
	end
end
print(k, "out of", maxi, "pty's opened and closed ok")
