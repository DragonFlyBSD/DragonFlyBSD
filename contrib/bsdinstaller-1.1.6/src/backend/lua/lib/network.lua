-- lib/network.lua
-- $Id: network.lua,v 1.10 2005/03/27 01:19:41 cpressey Exp $
-- Lua abstraction for the Network Interfaces of a system.

-- BEGIN lib/network.lua --

require "app"
require "cmdchain"

--[[------------------]]--
--[[ NetworkInterface ]]--
--[[------------------]]--

--
-- This class returns an object which represents one of the system's
-- network interfaces.
--
-- This class is not typically instantiated directly by client code.
-- Instead, the user should call NetworkInterface.all() to get a table
-- of all network interfaces present in the system, and choose one.
--
NetworkInterface = {}
NetworkInterface.new = function(name)
	local ni = {}			-- instance variable
	local desc = name		-- description of device
	local up			-- internal state...
	local mtu
	local inet6, prefixlen, scopeid
	local inet, netmask, broadcast
	local ether

	local toboolean = function(x)
		if x then
			return true
		else
			return false
		end
	end

	--
	-- Probe this network interface for its current state.
	--
	-- dc0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> mtu 1500
	-- inet6 fe80::250:bfff:fe96:cf68%dc0 prefixlen 64 scopeid 0x1 
	-- inet 10.0.0.19 netmask 0xffffff00 broadcast 10.0.0.255
	-- ether 00:50:bf:96:cf:68
	-- media: Ethernet autoselect (10baseT/UTP)
	--
	ni.probe = function(ni)
		local found, len, cap, flagstring
		local pty

		up = nil
		mtu = nil
		inet6, prefixlen, scopeid = nil, nil, nil
		inet, netmask, broadcast = nil, nil, nil

		pty = Pty.open(App.expand("${root}${IFCONFIG} ${name}", { name = name }))
		if not pty then
			return nil, "could not open pty"
		end
		line = pty:readline()
		while line do
			found, len, cap, flagstring =
			    string.find(line, "flags%s*=%s*(%d+)%s*%<([^%>]*)%>")
			if found then
				flagstring = "," .. flagstring .. ","
				up = toboolean(string.find(flagstring, ",UP,"))
			end

			found, len, cap = string.find(line, "mtu%s*(%d+)")
			if found then
				mtu = tonumber(cap)
			end
			found, len, cap = string.find(line, "inet6%s*([^%s]+)")
			if found then
				inet6 = cap
			end
			found, len, cap = string.find(line, "prefixlen%s*(%d+)")
			if found then
				prefixlen = tonumber(cap)
			end
			found, len, cap = string.find(line, "scopeid%s*0x(%x+)")
			if found then
				scopeid = cap
			end
			found, len, cap = string.find(line, "inet%s*(%d+%.%d+%.%d+%.%d+)")
			if found then
				inet = cap
			end
			found, len, cap = string.find(line, "netmask%s*0x(%x+)")
			if found then
				netmask = cap
			end
			found, len, cap = string.find(line, "broadcast%s*(%d+%.%d+%.%d+%.%d+)")
			if found then
				broadcast = cap
			end
			found, len, cap = string.find(line, "ether%s*(%x%x%:%x%x%:%x%x%:%x%x%:%x%x%:%x%x%)")
			if found then
				ether = cap
			end
			line = pty:readline()
		end
		pty:close()
	end

	--
	-- Accessor methods.
	--

	ni.is_up = function(ni)
		return up
	end

	ni.get_name = function(ni)
		return name
	end

	ni.get_inet_addr = function(ni)
		return inet
	end

	ni.get_netmask = function(ni)
		return netmask
	end

	ni.get_broadcast_addr = function(ni)
		return broadcast
	end

	ni.get_ether_addr = function(ni)
		return ether
	end

	ni.get_desc = function(ni)
		return desc
	end

	ni.set_desc = function(ni, new_desc)
		--
		-- Calculate a score for how well this string describes
		-- a network interface.  Reject obviously bogus descriptions
		-- (usually harvested from error messages in dmesg.)
		--
		local calculate_score = function(s)
			local score = 0

			-- In the absence of any good discriminator,
			-- the longest description wins.
			score = string.len(s)

			-- Look for clues
			if string.find(s, "%<.*%>") then
				score = score + 10
			end

			-- Look for irrelevancies
			if string.find(s, "MII bus") then
				score = 0
			end

			return score
		end

		if calculate_score(new_desc) > calculate_score(desc) then
			desc = new_desc
		end
	end

	--
	-- Set the description of a device, as best we can, based on
	-- the available system information.
	--
	ni.auto_describe = function(ni)
		--
		-- First give some common pseudo-devices some
		-- reasonable 'canned' descriptions.
		--
		local descs = {
		    ["ppp%d+"] = "Point-to-Point Protocol device",
		    ["sl%d+"] = "Serial Line IP device",
		    ["faith%d+"] = "IPv6-to-IPv4 Tunnel device",
		    ["lp%d+"] = "Network Line Printer device",
		    ["lo%d+"] = "Loopback device"
		}
		for pat, desc in descs do
			if string.find(name, "^" .. pat .. "$") then
				ni:set_desc(name .. ": " .. desc)
			end
		end

		--
		-- Now look through dmesg.boot for the names of
		-- physical network interface hardware.
		--
		for line in io.lines("/var/run/dmesg.boot") do
			local found, len, cap =
			    string.find(line, "^" .. name .. ":.*(%<.*%>).*$")
			if found then
				ni:set_desc(name .. ": " .. cap)
			end
		end
	end

	--
	-- CmdChain-creating methods.
	--

	ni.cmds_assign_inet_addr = function(ni, cmds, addr)
		cmds:add({
		    cmdline = "${root}${IFCONFIG} ${name} ${addr}",
		    replacements = {
			name = name,
			addr = addr
		    }
		})
	end

	ni.cmds_assign_netmask = function(ni, cmds, netmask)
		cmds:add({
		    cmdline = "${root}${IFCONFIG} ${name} netmask ${netmask}",
		    replacements = {
			name = name,
			netmask = netmask
		    }
		})
	end

	ni.cmds_dhcp_configure = function(ni, cmds)
		cmds:add(
		    {
			cmdline = "${root}${KILLALL} dhclient",
			failure_mode = CmdChain.FAILURE_IGNORE
		    },
		    {
			cmdline = "${root}${DHCLIENT} -1 ${name}",
			replacements = {
			    name = name
			}
		    }
		)
	end

	return ni
end

--[[-------------------]]--
--[[ NetworkInterfaces ]]--
--[[-------------------]]--

--
-- A container/aggregate class.  Contains a bunch of NetworkInterface
-- objects, typically the set of those available on a given system.
--

NetworkInterfaces = {}

NetworkInterfaces.new = function()
	local ni_tab = {}
	local method = {}

	method.add = function(nis)
		ni_tab[ni.name] = ni
	end

	method.get = function(nis, name)
		return ni_tab[name]
	end

	-- Iterator, typically used in for loops.
	method.each = function(nis)
		local name, ni
		local list = {}
		local i, n = 0, 0
		
		for name, ni in ni_tab do
			table.insert(list, ni)
			n = n + 1
		end

		table.sort(list, function(a, b)
			return a:get_name() < b:get_name()
		end)

		return function()
			if i <= n then
				i = i + 1
				return list[i]
			end
		end
	end

	--
	-- Populate the NetworkInterface collection with all the
	-- network interfaces detected as present in the system.
	--
	method.probe = function(nis)
		local pty, line, name, ni, line, pat, desc

		pty = Pty.open(App.expand("${root}${IFCONFIG} -l"))
		if not pty then
			return nil, "could not open pty"
		end
		line = pty:readline()
		pty:close()

		ni_tab = {}

		for name in string.gfind(line, "%s*([^%s]+)%s*") do
			ni = NetworkInterface.new(name)
			ni:probe()
			ni:auto_describe()
			ni_tab[name] = ni
		end

		return nis
	end

	--
	-- Returns the number of configured IP addresses of all the
	-- NetworkInterface objects in this NetworkInterfaces object.
	--
	-- Practically, used for asking "Uh, is the network on?"  :)
	--
	method.ip_addr_count = function(nis)
		local name, ni
		local num = 0

		for name, ni in ni_tab do
			ip_addr = ni:get_inet_addr()
			if ip_addr and not string.find(ip_addr, "^127%..*$") then
				num = num + 1
			end
		end

		return num
	end

	--
	-- User-interface methods.
	--

	--
	-- Display a dialog box, allow the user to select which
	-- network interface they want to use.
	--
	method.ui_select_interface = function(nis, tab)
		local actions, ni, ifname
		if not tab then tab = {} end
		local ui = tab.ui or App.ui
		local id = tab.id or "select_interface"
		local name = tab.name or _("Select Network Interface")
		local short_desc = tab.short_desc or _(
		    "Please select the network interface you wish to configure."
		)

		--
		-- Get interface list.
		--
		actions = {}
		for ni in nis:each() do
			table.insert(actions, {
			    id = ni:get_name(),
			    name = ni:get_desc()
			})
		end
		table.insert(actions, {
		    id = "cancel",
		    name = "Cancel"
		})
	
		ifname = App.ui:present({
		    id = id,
		    name =  name,
		    short_desc = short_desc,
		    role = "menu",
		    actions = actions
		}).action_id
	
		if ifname == "cancel" then
			return nil
		else
			return nis:get(ifname)
		end
	end

	return method
end

-- END of lib/network.lua --
