-- $Id: clock.lua,v 1.1 2005/02/01 21:58:24 cpressey Exp $

for i = 1, 30 do
	print("Waiting " .. (i * 50000000) .. " nanoseconds ...")
	Clock.nanosleep(0, i * 50000000)
end

for i = 1, 30 do
	print("Waiting " .. (i * 0.05) .. " seconds ...")
	Clock.nanosleep(i * 0.05)
end
