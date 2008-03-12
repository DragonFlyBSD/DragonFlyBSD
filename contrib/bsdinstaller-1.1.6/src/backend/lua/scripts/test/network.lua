-- $Id: network.lua,v 1.2 2005/02/23 23:07:01 cpressey Exp $

require "network"

App.add_conf_path("./scripts/demo/conf")	-- XXX BAD HACK
App.start()

netif = NetworkInterface.all()

for name, ni in netif do
	print(name .. ":")
	print("", "up = ", tostring(ni:is_up()))
	print("", "inet = ", tostring(ni:get_inet_addr()))
end

App.stop()
