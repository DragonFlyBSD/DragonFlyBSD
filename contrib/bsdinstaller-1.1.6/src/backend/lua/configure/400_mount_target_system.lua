-- $Id: 400_mount_target_system.lua,v 1.13 2005/02/24 23:08:03 cpressey Exp $

require "target_system"

return {
    name = "mount_target_system",
    title = "Mount Target System",
    action = function(fsm)
	--
	-- If there is a target system mounted, unmount it before starting.
	--
	if App.state.target ~= nil and App.state.target:is_mounted() then
		if not App.state.target:unmount() then
			App.ui:inform(
			    _("Warning: already-mounted target system could " ..
			      "not be correctly unmounted first.")
			)
			return fsm:prev()
		end
	end

	App.state.target = TargetSystem.new(App.state.sel_part, "mnt")
	if not App.state.target:probe() then
		App.ui:inform(_(
		    "The target system could not be successfully probed."
		))
		return fsm:prev()
	end
	if not App.state.target:mount() then
		App.ui:inform(_(
		    "The target system could not be successfully mounted."
		))
		return fsm:prev()
	end

	return fsm:next()
    end
}
