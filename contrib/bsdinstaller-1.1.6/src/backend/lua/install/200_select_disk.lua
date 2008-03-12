-- $Id: 200_select_disk.lua,v 1.16 2005/02/24 23:08:04 cpressey Exp $

require "gettext"
require "storage_ui"

return {
    name = "select_disk",
    title = "Select Disk",
    action = function(fsm)
	App.state.sel_disk = nil
	App.state.sel_part = nil

	-- XXX there might be a better place to handle this.
	if App.state.storage:get_disk_count() == 0 then
		App.ui:inform(_(
		    "The installer could not find any disks suitable "	..
		    "for installation (IDE or SCSI) attached to this "	..
		    "computer.  If you wish to install %s"		..
		    " on an unorthodox storage device, you will have to " ..
		    "exit to a LiveCD command prompt and install it "	..
		    "manually, using the file /README as a guide.",
		    App.os.name)
		)
		return nil
	end

	local dd = StorageUI.select_disk({
	    sd = App.state.storage,
	    short_desc = _("Select a disk on which to install %s.",
	        App.os.name),
	    cancel_desc = _("Return to %s", fsm:prev().title)
	})

	if dd then
		if dd:is_mounted() then
			App.ui:inform(
			    _("One or more subpartitions of one or more " ..
			    "primary partitions of the selected disk "	  ..
			    "are already in use (they are currently "	  ..
			    "mounted on mountpoints in the filesystem.) " ..
			    "You should either unmount them before "	  ..
			    "proceeding, or select a different disk to "  ..
			    "install %s on.", App.os.name))
			return fsm:current()
		end

		App.state.sel_disk = dd

		-- XXX there might be a better place to handle this.
		if dd:get_capacity() < App.state.disk_min then
			App.ui:inform(_(
			    "WARNING: you should have a disk "	..
			    "at least %dM in size, or "		..
			    "you may encounter problems trying to " ..
			    "install %s.",
			    App.state.disk_min, App.os.name)
			)
		end

		return fsm:next()
	else
		return fsm:prev()
	end
    end
}
