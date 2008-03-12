-- $Id: 600_unmount_target_system.lua,v 1.4 2005/02/24 23:08:03 cpressey Exp $

require "target_system"

return {
    name = "unmount_target_system",
    title = "Unmount Target System",
    action = function(fsm)
	if App.state.target:unmount() then
		return fsm:next()
	else
		App.ui:inform("Target system could not be unmounted!")
		return nil
	end
    end
}
