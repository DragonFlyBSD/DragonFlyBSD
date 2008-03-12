-- $Id: 800_configure_network.lua,v 1.7 2005/03/27 01:20:40 cpressey Exp $

require "gettext"
require "network"
require "configvars"

-- XXX Several of the functions in this script should be in
-- XXX scripts/demo/lib/network_ui.lua (or something) instead.

local execute_and_wait_for = function(ni, cmds)
	if cmds:execute() then
		if App.wait_for{
		    predicate = function()
			ni:probe()
			return ni:is_up()
		    end,
		    timeout = 30,
		    frequency = 2,
		    short_desc = _(
			"Waiting for interface %s to come up...",
			ni:get_name()
		    )
		} then
			-- show_ifconfig(a->c, interface);
			App.ui:inform(_(
			   "Interface\n\n%s\n\nis now up, with IP address %s.",
			   ni:get_desc(), ni:get_inet_addr()
			))
			return true
		else
			-- show_ifconfig(a->c, interface);
			App.ui:inform(_(
			   "Interface\n\n%s\n\nfailed to come up.",
			   ni:get_desc()
			))
			return false
		end
	else
		return false
	end
end

local dhcp_configure = function(ni)
	local cmds = CmdChain.new()

	ni:cmds_dhcp_configure(cmds)
	return execute_and_wait_for(ni, cmds)
end

local static_configure = function(ni)
	local response = App.ui:present{
	    id = "assign_ip",
	    name = _("Assign IP Address"),
	    short_desc = _("Configuring Interface:"),
	    fields = {
	        {
		    id = "ip",
		    name = _("IP Address"),
		    short_desc = _("Enter the IP Address you would like to use")
		},
	        {
		    id = "netmask",
		    name = _("Netmask"),
		    short_desc = _("Enter the netmask of the IP address")
		},
	        {
		    id = "default_router",
		    name = _("Default Router"),
		    short_desc = _("Enter the IP address of the default router")
		},
	        {
		    id = "primary_dns",
		    name = _("Primary DNS Server"),
		    short_desc = _("Enter the IP address of primary DNS Server")
		},
	        {
		    id = "host",
		    name = _("Hostname")
		},
	        {
		    id = "domain",
		    name = _("Domain Name")
		}
	    },
	    actions = {
	        {
		    id = "ok",
		    name = _("Configure Interface")
		},
	        {
		    id = "cancel",
		    name = _("Return to Utilities Menu")
		}
	    },
	    datasets = {
	        {
		    ip = "",
		    netmask = "",
		    default_router = "",
		    primary_dns = "",
		    host = "",
		    domain = ""
		}
	    }
	}

	if response.action_id == "ok" then
		local ip = response.datasets[1].ip
		local netmask = response.datasets[1].netmask
		local default_router = response.datasets[1].default_router
		local primary_dns = response.datasets[1].primary_dns
		local host = response.datasets[1].host
		local domain = response.datasets[1].domain
	
		local cmds = CmdChain.new()
		-- XXX check ip for wellformedness first
		ni:cmds_assign_inet_addr(cmds, ip)
		ni:cmds_assign_netmask(cmds, netmask)

		cmds:add({
		    cmdline = "${root}${ROUTE} add default ${default_router}",
		    replacements = {
		        default_router = default_router
		    }
		})

		local success = execute_and_wait_for(ni, cmds)

		-- XXX this isn't quite right, yet...
		-- XXX nowhere to write these yet! :)
		local rc_conf = ConfigVars.new()

		rc_conf:set(
		    string.format("ifconfig_%s", ni:get_name()),
		    string.format("inet %s netmask %s", ip, netmask)
		)
		rc_conf:set("defaultrouter", default_router)

		rc_conf:set("hostname", string.format("%s.%s", host, domain))

		local resolv_conf = ConfigVars.new()

		resolv_conf:set("search", domain)
		resolv_conf:set("nameserver", primary_dns)
	end
end

return {
    name = "network",
    title = "Configure your Network",
    action = function(fsm)
	local actions, ifname, ni, result

	if not App.state.net_if then
		App.state.net_if = NetworkInterfaces.new()
		App.state.net_if:probe()
	end

	if App.state.net_if:ip_addr_count() > 0 then
		return fsm:next()
	end

	if not App.ui:confirm(_(
	    "You have not yet configured your network settings. "	..
	    "Would you like to do so now? (Having an operational "	..
	    "network connection will enhance the ability of "		..
	    "subsequent tasks, such as installing."
	)) then
		return fsm:next()
	end

	ni = App.state.net_if:ui_select_interface()
	if not ni then
		return fsm:next()
	end

	if App.ui:confirm(_(
	    "Would you like to try dynamically configuring this interface? " ..
	    "(This requires that you have a DHCP server operating on " ..
	    "the network that this interface is attached to.)"
	)) then
		dhcp_configure(ni)
	else
		static_configure(ni)
	end

	return fsm:next()
    end
}
