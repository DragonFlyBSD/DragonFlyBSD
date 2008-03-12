-- $Id: mountpoints.lua,v 1.4 2005/02/24 23:08:03 cpressey Exp $

-- Default configuration file for suggested mountpoints.
--
-- Note that this file should return a function which takes two
-- numbers (the capacity of the partition and the capacity of
-- RAM, both in megabytes) and should return a list of tables,
-- each like:
--
-- {
--   mountpoint = "/foo",    -- name of mountpoint
--   capstring  = "123M"     -- suggested capacity
-- }
--
-- Note that the capstring can be "*" to indicate 'use the
-- rest of the partition.')

return function(part_cap, ram_cap)

	--
	-- First, calculate suggested swap size:
	--
	local swap = 2 * ram_cap
	if ram_cap > (part_cap / 2) or part_cap < 4096 then
		swap = ram_cap
	end
	swap = tostring(swap) .. "M"

	--
	-- Now, based on the capacity of the partition,
	-- return an appropriate list of suggested mountpoints.
	--
	if part_cap < 523 then
		return {
			{ mountpoint = "/",	capstring = "70M" },
			{ mountpoint = "swap",	capstring = swap },
			{ mountpoint = "/var",	capstring = "32M" },
			{ mountpoint = "/tmp",	capstring = "32M" },
			{ mountpoint = "/usr",	capstring = "174M" },
			{ mountpoint = "/home",	capstring = "*" }
		}
	elseif part_cap < 1024 then
		return {
			{ mountpoint = "/",	capstring = "96M" },
			{ mountpoint = "swap",	capstring = swap },
			{ mountpoint = "/var",	capstring = "64M" },
			{ mountpoint = "/tmp",	capstring = "64M" },
			{ mountpoint = "/usr",	capstring = "256M" },
			{ mountpoint = "/home",	capstring = "*" }
		}
	elseif part_cap < 4096 then
		return {
			{ mountpoint = "/",	capstring = "128M" },
			{ mountpoint = "swap",	capstring = swap },
			{ mountpoint = "/var",	capstring = "128M" },
			{ mountpoint = "/tmp",	capstring = "128M" },
			{ mountpoint = "/usr",	capstring = "512M" },
			{ mountpoint = "/home",	capstring = "*" }
		}
	elseif part_cap < 10240 then
		return {
			{ mountpoint = "/",	capstring = "256M" },
			{ mountpoint = "swap",	capstring = swap },
			{ mountpoint = "/var",	capstring = "256M" },
			{ mountpoint = "/tmp",	capstring = "256M" },
			{ mountpoint = "/usr",	capstring = "3G" },
			{ mountpoint = "/home",	capstring = "*" }
		}
	else
		return {
			{ mountpoint = "/",	capstring = "256M" },
			{ mountpoint = "swap",	capstring = swap },
			{ mountpoint = "/var",	capstring = "256M" },
			{ mountpoint = "/tmp",	capstring = "256M" },
			{ mountpoint = "/usr",	capstring = "8G" },
			{ mountpoint = "/home",	capstring = "*" }
		}
	end
end
