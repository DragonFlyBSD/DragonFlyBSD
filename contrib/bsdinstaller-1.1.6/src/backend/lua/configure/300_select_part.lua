-- $Id: 300_select_part.lua,v 1.4 2005/02/24 23:08:03 cpressey Exp $

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
		"Select the primary partition of %s " ..
		"on which the installation of %s resides.",
		App.state.sel_disk:get_name(),
		App.os.name),
	   cancel_desc = _("Return to %s", fsm:prev().title)
	})

	if pd then
		App.state.sel_part = pd
		return fsm:next()
	else
		return fsm:prev()
	end
    end
}
