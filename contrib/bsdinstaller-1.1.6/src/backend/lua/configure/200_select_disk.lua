-- $Id: 200_select_disk.lua,v 1.5 2005/02/24 23:08:03 cpressey Exp $

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
		    "The installer could not find any suitable disks "	..
		    "attached to this computer.  If you wish to "	..
		    "configure an installation of %s "			..
		    "on an unorthodox storage device, you will have to " ..
		    "exit to a LiveCD command prompt and configure it "	..
		    "manually, using the file /README as a guide.",
		    App.os.name)
		)
		return nil
	end

	local dd = StorageUI.select_disk({
	    sd = App.state.storage,
	    short_desc = _(
	        "Select the disk on which the installation of %s " ..
		"that you wish to configure resides.",
	        App.os.name),
	    cancel_desc = _("Return to Main") -- _("Return to %s", fsm:prev().title)
	})

	if dd then
		App.state.sel_disk = dd
		return fsm:next()
	else
		return nil -- fsm:prev()
	end
    end
}
