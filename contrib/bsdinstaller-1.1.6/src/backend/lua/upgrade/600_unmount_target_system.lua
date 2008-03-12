-- $Id: 600_unmount_target_system.lua,v 1.1 2005/03/27 23:19:29 cpressey Exp $

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
