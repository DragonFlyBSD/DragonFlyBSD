-- $Id: 270_install_bootblocks.lua,v 1.6 2005/02/24 23:08:04 cpressey Exp $

require "gettext"

return {
    name = "install_bootblocks",
    title = "Install Bootblocks",
    action = function(fsm)
	local datasets_list = {}
	local dataset
	local dd
	local disk_ref = {}	-- map from raw name to ref to DiskDescriptor

	for dd in App.state.storage:get_disks() do
		local raw_name = dd:get_raw_device_name()

		disk_ref[raw_name] = dd

		dataset = {
			disk = raw_name,
			boot0cfg = "Y",
			packet = "N"
		}
		
		if dd:get_capacity() > 8192 then
			dataset.packet = "Y"
		end

		table.insert(datasets_list, dataset)
	end

	local response = App.ui:present({
	    id = "install_bootstrap",
	    name = _("Install Bootblock(s)"),
	    short_desc = _(
		"You may now wish to install bootblocks on one or more disks. "	..
		"If you already have a boot manager installed, you can skip "	..
		"this step (but you may have to configure your boot manager "	..
		"separately.)  If you installed %s on a disk other "		..
		"than your first disk, you will need to put the bootblock "	..
		"on at least your first disk and the %s disk.",
		App.os.name, App.os.name),
	    long_desc = _(
	        "'Packet Mode' refers to using newer BIOS calls to boot " ..
	        "from a partition of the disk.  It is generally not " ..
	        "required unless:\n\n" ..
	        "- your BIOS does not support legacy mode; or\n" ..
	        "- your %s primary partition resides on a " ..
	        "cylinder of the disk beyond cylinder 1024; or\n" ..
	        "- you just can't get it to boot without it.",
		App.os.name
	    ),
	    special = "bsdinstaller_install_bootstrap",

	    fields = {
		{
		    id = "disk",
		    name = _("Disk Drive"),
		    short_desc = _("The disk on which you wish to install a bootblock"),
		    editable = "false"
		},
		{
		    id = "boot0cfg",
		    name = _("Install Bootblock?"),
		    short_desc = _("Install a bootblock on this disk"),
		    control = "checkbox"
		},
		{
		    id = "packet",
		    name = _("Packet mode?"),
		    short_desc = _("Select this to use 'packet mode' to boot the disk"),
		    control = "checkbox"
		}
	    },
	
	    actions = {
		{
		    id = "ok",
		    name = _("Accept and Install Bootblocks")
		},
		{
		    id = "cancel",
		    name = _("Skip this Step")
		}
	    },

	    datasets = datasets_list,

	    multiple = "true",
	    extensible = "false"
	})

	if response.action_id == "ok" then
		local cmds = CmdChain.new()
		local i

		for i, dataset in response.datasets do
			if dataset.boot0cfg == "Y" then
				dd = disk_ref[dataset.disk]
				dd:cmds_install_bootblock(cmds,
				    (dataset.packet == "Y"))
			end
		end

		cmds:execute()
	end

	return fsm:next()
    end
}
