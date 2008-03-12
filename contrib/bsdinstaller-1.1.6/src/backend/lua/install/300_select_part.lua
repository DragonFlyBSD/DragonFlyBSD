-- $Id: 300_select_part.lua,v 1.17 2005/02/24 23:08:04 cpressey Exp $

require "gettext"
require "storage_ui"

return {
    name = "select_part",
    title = "Select Partition",
    action = function(fsm)
	App.state.sel_part = nil

	local pd = StorageUI.select_part({
	    dd = App.state.sel_disk,
	    short_desc = _(
		"Select the primary partition of %s (also "	..
		"known as a `slice' in the BSD tradition) on "	..
		"which to install %s.",
		App.state.sel_disk:get_name(),
		App.os.name),
	   cancel_desc = _("Return to %s", fsm:prev().title)
	})

	if pd then
		if pd:is_mounted() then
			App.ui:inform(
			    _("One or more subpartitions on the selected "	..
			    "primary partition already in use (they are "	..
			    "currently mounted in the filesystem.) "		..
			    "You should either unmount them before "		..
			    "proceeding, or select a different partition "	..
			    "or disk on which to install %s.", App.os.name))
			return fsm:current()
		end

		if pd:get_activated_swap() > 0 then
			local choices = {}

			choices[_("Reboot")] = "reboot"
			choices[_("Return to %s", fsm:prev().title)] = fsm:prev()

			return App.ui:select(
			    _("Some subpartitions on the selected primary "	..
			    "partition are already activated as swap. "		..
			    "Since there is no way to deactivate swap in "	..
			    "%s once it is activated, in order "		..
			    "to edit the subpartition layout of this "		..
			    "primary partition, you must first reboot.",
			    App.os.name), choices)
		end

		App.state.sel_part = pd

		if pd:get_capacity() < App.state.disk_min then
			App.ui:inform(_(
			    "WARNING: you should have a primary "	..
			    "partition at least %dM in size, or "	..
			    "you may encounter problems trying to "	..
			    "install %s.",
			    App.state.disk_min, App.os.name)
			)
		end

		if App.state.sel_disk:has_been_touched() or
		   App.ui:confirm(_(
		    "WARNING!  ALL data in primary partition #%d,\n\n%s\n\non the "	..
		    "disk\n\n%s\n\n will be IRREVOCABLY ERASED!\n\nAre you "		..
		    "ABSOLUTELY SURE you wish to take this action?  This is "		..
		    "your LAST CHANCE to cancel!",
		    pd:get_number(), pd:get_desc(),
		    App.state.sel_disk:get_desc())) then
			local cmds = CmdChain.new()

			pd:cmds_format(cmds)
			if cmds:execute() then
				App.ui:inform(_(
				    "Primary partition #%d was formatted.",
				    pd:get_number())
				)
				return fsm:next()
			else
				App.ui:inform(_(
				    "Primary partition #%d was "	..
				    "not correctly formatted, and may "	..
				    "now be in an inconsistent state. "	..
				    "We recommend re-formatting it "	..
				    "before proceeding.",
				    pd:get_number())
				)
				return fsm:current()
			end
		else
			App.ui:inform(_(
			    "Action cancelled - " ..
			    "no primary partitions were formatted."))
			return fsm:prev()
		end
	else
		return fsm:prev()
	end
    end
}
