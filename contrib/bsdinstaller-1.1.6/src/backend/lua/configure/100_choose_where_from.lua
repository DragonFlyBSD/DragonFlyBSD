-- $Id: 100_choose_where_from.lua,v 1.3 2005/02/24 23:08:03 cpressey Exp $

require "target_system"

return {
    name = "choose_target_system",
    title = "Choose Target System",
    action = function(fsm)
	local action_id = App.ui:present({
	    id = "choose_target_system",
	    name = _("Choose Target System"),
	    short_desc = _(
	        "Please choose which installed system you want to configure."
	    ),
	    actions = {
		{
		    id = "this",
		    name = _("Configure the Running System")
		},
		{
		    id = "disk",
		    name = _("Configure a System on Disk")
		},
		{
		    id = "cancel",
		    name = _("Cancel"),
		}
	    },
	    role = "menu"
	}).action_id

	if action_id == "cancel" then
		return nil
	end

	if action_id == "disk" then
		return fsm:next()
	end

	App.state.target = TargetSystem.new()
	App.state.target:use_current()

	-- Jump straight to the menu.
	return "configuration_menu"
    end
}
