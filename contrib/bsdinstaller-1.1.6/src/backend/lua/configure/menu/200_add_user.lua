-- $Id: 200_add_user.lua,v 1.7 2005/03/29 12:12:15 den Exp $

require "gettext"

local is_gecos_clean = function(gecos)
	local i, char

	i = 1
	while i <= string.len(gecos) do
		char = string.sub(gecos, i, i)
		if string.find(char, "%c") or		  -- no ctrl chars
		   string.byte(char) == 127 or		  -- no 'DEL' char
		   string.find(":!@", char, 1, true) then -- none of these
			return false
		end
		i = i + 1
	end

	return true
end

local is_name_clean = function(name)
	local i, char

	i = 1
	while i <= string.len(name) do
		char = string.sub(name, i, i)
		if string.find(char, "%c") or		-- no ctrl chars
		   string.byte(char) == 127 or		-- no 'DEL' char
		   string.byte(char) > 127 or		-- no 8-bit chars
							-- and none of these:
		   string.find(" ,\t:+&#%^()!@~*?<>=|\\/\"", char, 1, true) or
		   (char == "-" and i == 1) or		-- no '-' at start
							-- '$' only at end:
		   (char == "$" and i ~= string.len(name)) then
			return false
		end
		i = i + 1
	end

	return true
end

local add_user = function()
	local done = false
	local result
	local cmds

	local form = {
	    id = "add_user",
	    name = _("Add User"),
	    short_desc = _("Here you can add a user to an installed system.\n\n" ..
		"You can leave the Home Directory, User ID, and Login Group "	..
		"fields empty if you want these items to be automatically "	..
		"allocated by the system."),
	    fields = {
		{
		    id = "username",
		    name = _("Username"),
		    short_desc = _("Enter the username the user will log in as")
		},
		{
		    id = "gecos",
		    name = _("Real Name"),
		    short_desc = _("Enter the real name (or GECOS field) of this user")
		},
		{
		    id = "passwd_1",
		    name = _("Password"),
		    short_desc = _("Enter the user's password (will not be displayed)"),
		    obscured = "true"
		},
		{
		    id = "passwd_2",
		    name = _("Password (Again)"),
		    short_desc = _("Re-enter the user's password to confirm"),
		    obscured = "true"
		},
		{
		    id = "shell",
		    name = _("Shell"),
		    short_desc = _("Enter the full path to the user's shell program")
		},
		{
		    id = "home",
		    name = _("Home Directory"),
		    short_desc = _("Enter the full path to the user's home directory, or leave blank")
		},
		{
		    id = "uid",
		    name = _("User ID"),
		    short_desc = _("Enter this account's numeric user id, or leave blank")
		},
		{
		    id = "group",
		    name = _("Login Group"),
		    short_desc = _("Enter the primary group for this account, or leave blank")
		},
		{
		    id = "groups",
		    name = _("Other Group Memberships"),
		    short_desc = _(
		        "Enter a comma-separated list of other groups "	..
			"that this user should belong to"
		    )
		}
	    },
	    actions = {
		{
		    id = "ok",
		    name = _("Accept and Add User")
		},
		{
		    id = "cancel",
		    name = _("Return to Configure Menu")
		}
	    },
	    datasets = {
		{
		    username = "",
		    gecos = "",
		    passwd_1 = "",
		    passwd_2 = "",
		    shell = "/bin/tcsh",
		    home = "",
		    uid = "",
		    group = "",
		    groups = ""
		}
	    }
	}

	while not done do
		result = App.ui:present(form)
		if result.action_id == "ok" then
			form.datasets = result.datasets

			--
			-- Fetch form field values.
			--
			local username	= result.datasets[1].username
			local gecos	= result.datasets[1].gecos
			local passwd_1	= result.datasets[1].passwd_1
			local passwd_2	= result.datasets[1].passwd_2
			local shell	= result.datasets[1].shell
			local home	= result.datasets[1].home
			local uid	= result.datasets[1].uid
			local group	= result.datasets[1].group
			local groups	= result.datasets[1].groups

			--
			-- Valid field values.
			--

			if string.len(username) == 0 then
				App.ui:inform(_(
				    "You must enter a username."
				))
			elseif passwd_1 ~= passwd_2 then
				App.ui:inform(_(
				    "The passwords do not match."
				))
			elseif not is_name_clean(username) then
				App.ui:inform(_(
				    "The username contains illegal characters."
				))
			elseif not is_gecos_clean(gecos) then
				App.ui:inform(_(
				    "The text specified in the Real Name " ..
				    "field contains illegal characters."
				))
			elseif not is_name_clean(group) then
				App.ui:inform(_(
				    "The name of the login group contains " ..
				    "illegal characters."
				))
			--[[
		    !assert_clean(a->c, _("Home Directory"), home, FILENAME_NOT_ALLOWED) ||
		    !assert_clean(a->c, _("User ID"), uid, PW_NOT_ALLOWED) ||
		    !assert_clean(a->c, _("Group Memberships"), groups, MEMBERSHIPS_NOT_ALLOWED)) {
			done = 0;
			]]--
			elseif not FileSystem.is_program(App.dir.root .. shell) and
			       shell ~= "/nonexistent" then
				App.ui:inform(_(
				    "The selected shell does not exist on the system."
				))
			else
				local cmds = CmdChain.new()

				App.state.target:cmds_add_user(cmds, {
				    username = username,
				    gecos = gecos,
				    shell = shell,
				    uid = uid,
				    group = group,
				    home = home,
				    groups = groups,
				    password = passwd_1
				})
				if cmds:execute() then
					App.ui:inform(_(
					    "User `%s' was added.",
					    username
					))
					done = true
				else
					App.ui:inform(_(
					    "User was not successfully added."
					))
				end
			end
		else
			-- Cancelled.
			done = true
		end
	end
end

return {
    name = _("Add User"),
    effect = function()
	add_user()
	return Menu.CONTINUE
    end
}
