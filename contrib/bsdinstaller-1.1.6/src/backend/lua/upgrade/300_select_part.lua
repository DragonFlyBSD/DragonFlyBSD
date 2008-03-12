-- $Id: 300_select_part.lua,v 1.1 2005/03/27 23:19:29 cpressey Exp $

return {
    name = "select_part",
    title = "Select Partition",
    action = function(fsm)
	App.state.sel_part = nil

	local pd = StorageUI.select_part({
	    dd = App.state.sel_disk,
	    short_desc = _(
		"Select the primary partition of %s " ..
		"on which the installation that " ..
		"you wish to upgrade resides.",
		App.state.sel_disk:get_name()),
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
