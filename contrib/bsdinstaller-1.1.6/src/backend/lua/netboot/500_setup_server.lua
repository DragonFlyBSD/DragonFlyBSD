-- $Id: 500_setup_server.lua,v 1.2 2005/03/29 12:12:15 den Exp $

-- Setup a remote boot installation environment where a machine
-- can boot via DHCP/TFTP/NFS and have a running environment
-- where the installer can setup the machine.

--
-- Create the commands necessary for setting up a remote boot server.
--
local start_netboot_server = function(cmds, tab)
	local nic = assert(tab.nic)
	local ip_addr = tab.ip_addr or "10.1.0.1"
	local netmask = tab.netmask or "255.255.255.0"

	cmds:add{
	    cmdline = "${root}${IFCONFIG} ${if_name} ${ip_addr} netmask ${netmask}",
	    replacements = {
		if_name = nic:get_name(),
		ip_addr = ip_addr,
		netmask = netmask
	    }
	}
	cmds:add(
	    "${root}${MKDIR} -p ${tmp}tftpdroot",
	    "${root}${CP} ${root}boot/pxeboot ${tmp}tftpdroot",
	    "${root}${ECHO} / -ro -alldirs >>${root}etc/exports",
	    "${root}${ECHO} /cdrom -ro -alldirs >>${root}etc/exports",
	    "${root}${ECHO} tftp dgram udp wait root " ..
		"${root}${TFTPD} tftpd -l -s ${tmp}tftpdroot " ..
		">>${root}etc/inetd.conf",
	    "${root}${INETD}",
	    "${root}${TOUCH} ${root}var/db/dhcpd.leases",
	    "${root}${DHCPD} >${root}dev/null 2>&1",
	    "${root}${RPCBIND} >${root}dev/null 2>&1",
	    "${root}${MOUNTD} -ln >${root}dev/null 2>&1",
	    "${root}${NFSD} -u -t -n 6 >${root}dev/null 2>&1"
	)
end

return {
    name = "setup_server",
    title = "Set Up NetBoot Server",
    action = function(fsm)

	-- Probe interfaces if not already done...
	if not App.state.net_if then
		App.state.net_if = NetworkInterfaces.new()
		App.state.net_if:probe()
	end

	local response = App.ui:present{
	    name = _("Enable NetBoot Installation Services?"),
	    short_desc =
	        _("NetBoot Installation Services allow this machine to become "	..
                  "an Installation Server that will allow other machines "	..
		  "on this network to boot as PXE clients, and will start the "	..
		  "Installation Environment on them.\n\n"			..
		  "*NOTE!*  This will assign the IP Address of 10.1.0.1/24 to "	..
		  "the selected interface.\n\n"					..
		  "Would you like to provision this machine to serve up the "	..
		  "Installation Environment to other machines?"),
	    actions = {
		{
		    id = "ok",
		    name = _("Enable NetBoot Installation Services")
		},
		{
		    id = "cancel",
		    name = _("No thanks")
		}
	    }
	}

	if response.action_id == "ok" then
		local ni
		
		ni = App.state.net_if:ui_select_interface()
		if not ni then
			return nil
		end

		local cmds = CmdChain.new()
		start_netboot_server(cmds, {
		    nic = ni,
		    ip_addr = "10.1.0.1"
		})
		if cmds:execute() then
			App.ui:inform(
			    _("NetBoot installation services are now started.")
			)
		else
			App.ui:inform(
			    _("A failure occured while provisioning "	..
			      "the NetBoot environment.  Please "	..
			      "consult the log file for details.")
			)
		end
	end

	return fsm:next()
    end
}
