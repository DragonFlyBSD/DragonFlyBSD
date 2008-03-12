-- $Id: 100_set_root_password.lua,v 1.6 2005/03/29 18:00:18 den Exp $

require "gettext"

local set_root_password = function()
	local done = false
	local result
	local cmds
	local form = {
	    id = "root_passwd",
	    name = _("Set Root Password"),
	    short_desc = _(
		"Here you can set the super-user (root) password."
	    ),

	    fields = {
		{
		    id = "root_passwd_1",
		    name = _("Root Password"),
		    short_desc = _("Enter the root password you would like to use"),
		    obscured = "true"
		},
		{
		    id = "root_passwd_2",
		    name = _("Re-type Root Password"),
		    short_desc = _("Enter the same password again to confirm"),
		    obscured = "true"
		}
	    },
	    
	    actions = {
		{
		    id = "ok",
		    name = _("Accept and Set Password")
		},
		{
		    id = "cancel",
		    name = _("Return to Configure Menu")
		}
	    },

	    datasets = {
		{ root_passwd_1 = "", root_passwd_2 = "" }
	    }
	}

	while not done do
		result = App.ui:present(form)

		if result.action_id == "ok" then
			form.datasets = result.datasets

			--
			-- Fetch form field values.
			--

			local root_passwd_1 = result.datasets[1].root_passwd_1
			local root_passwd_2 = result.datasets[1].root_passwd_2
			
			--[[
			if (!assert_clean(a->c, _("Root Password"), root_passwd_1, PW_NOT_ALLOWED)) {
				done = 0;
			]]--
			
			if root_passwd_1 == root_passwd_2 then
				--
				-- Passwords match, so set the root password.
				--
				cmds = CmdChain.new()
				App.state.target:cmds_set_password(cmds,
				    "root", root_passwd_1)
				if cmds:execute() then
					App.ui:inform(
					    _("The root password has been changed.")
					)
					done = true
				else
					App.ui:inform(
					    _("An error occurred when " ..
					      "setting the root password.")
					)
					done = false
				end
			else
				--
				-- Passwords don't match - tell the user, let them try again.
				--
				App.ui:inform(
				    _("The passwords do not match.")
				)
				done = false
			end
		else
			-- Cancelled
			done = true
		end
	end
end

return {
    name = _("Set Root Password"),
    effect = function()
	set_root_password()
	return Menu.CONTINUE
    end
}
