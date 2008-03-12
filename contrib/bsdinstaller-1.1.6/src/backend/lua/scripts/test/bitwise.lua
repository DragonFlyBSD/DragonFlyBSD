-- $Id: bitwise.lua,v 1.2 2005/02/22 02:54:32 cpressey Exp $

require "bitwise"

d = function(a, b)
	print(a, b,
	    "a & b = " .. Bitwise.bw_and(a, b),
	    "a | b = " .. Bitwise.bw_or(a, b)
	)
end

d(255, 64)
d(256, 63)
d(256, 256)
d(64, 255)
d(10, 12)
