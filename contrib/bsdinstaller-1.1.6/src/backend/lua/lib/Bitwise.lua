-- lib/bitwise.lua
-- $Id: Bitwise.lua,v 1.1 2005/02/22 02:54:32 cpressey Exp $
-- Package for (pure-Lua portable but extremely slow) bitwise arithmetic.

-- BEGIN lib/bitwise.lua --

--[[---------]]--
--[[ Bitwise ]]--
--[[---------]]--

local odd = function(x)
	return x ~= math.floor(x / 2) * 2
end

Bitwise = {}

Bitwise.bw_and = function(a, b)
	local c, pow = 0, 1
	while a > 0 or b > 0 do
		if odd(a) and odd(b) then
			c = c + pow
		end
		a = math.floor(a / 2)
		b = math.floor(b / 2)
		pow = pow * 2
	end
	return c
end

Bitwise.bw_or = function(a, b)
	local c, pow = 0, 1
	while a > 0 or b > 0 do
		if odd(a) or odd(b) then
			c = c + pow
		end
		a = math.floor(a / 2)
		b = math.floor(b / 2)
		pow = pow * 2
	end
	return c
end

-- END of lib/bitwise.lua --
