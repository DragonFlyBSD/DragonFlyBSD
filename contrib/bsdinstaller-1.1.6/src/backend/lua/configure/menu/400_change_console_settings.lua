-- $Id: 400_change_console_settings.lua,v 1.2 2005/02/24 23:08:03 cpressey Exp $

require "gettext"

local set_keyboard_map = function()
	local cmds, files, dir, filename, full_filename

	--
	-- Select a file.
	--
	dir = App.expand("${root}${base}usr/share/syscons/keymaps",
	    {
		base = App.state.target:get_base()
	    }
	)

	filename = App.ui:select_file{
	    title = _("Select Keyboard Map"),
	    short_desc = _(
		"Select a keyboard map appropriate to your keyboard layout."
	    ),
	    cancel_desc = _("Return to Utilities Menu"),
	    dir = dir,
	    extension = "kbd"
	}
	if filename == "cancel" then
		return false
	end
	filename = dir .. "/" .. filename

	cmds = CmdChain.new()
	cmds:add{
	    cmdline = "${root}${KBDCONTROL} -l ${filename} < /dev/ttyv0",
	    replacements = { filename = filename }
	}
	if cmds:execute() then
		--[[
			snprintf(filename, 256, "/usr/share/syscons/keymaps/%s", s);
			snprintf(keymapname, 256, filename_noext(basename(filename)));
			config_var_set(rc_conf, "keymap", keymapname);
		]]--

		return true
	else
		App.ui:inform(_(
		    "Errors occurred; keyboard map was not successfully set."
		))
	end
end

local set_video_font = function()
	local cmds, files, dir, filename, full_filename

	--
	-- Select a file.
	--
	dir = App.expand("${root}${base}usr/share/syscons/fonts",
	    {
		base = App.state.target:get_base()
	    }
	)

	filename = App.ui:select_file{
	    title = _("Select Console Font"),
	    short_desc = _("Select a font appropriate to your video monitor and language."),
	    cancel_desc = _("Return to Utilities Menu"),
	    dir = dir,
	    extension = "fnt"
	}
	if filename == "cancel" then
		return false
	end
	filename = dir .. "/" .. filename

	cmds = CmdChain.new()
	cmds:add{
	    cmdline = "${root}${VIDCONTROL} -f ${filename} < /dev/ttyv0",
	    replacements = { filename = filename }
	}
	if cmds:execute() then
		local found, len, w, h = string.find(filename, "(%d+)x(%d+)")
		if found then
			w = tonumber(w)
			h = tonumber(h)
			--[[
			snprintf(variable, 256, "font8x%d", by);
			snprintf(filename, 256, "/usr/share/syscons/fonts/%s", s);
			snprintf(fontname, 256, filename_noext(basename(filename)));
			config_var_set(rc_conf, variable, fontname);
			]]--
		end

		return true
	else
		App.ui:inform(_(
		    "Errors occurred; video font was not successfully set."
		))
	end
end


local set_screen_map = function()
	local cmds, files, dir, filename, full_filename

	--
	-- Select a file.
	--
	dir = App.expand("${root}${base}usr/share/syscons/scrnmaps",
	    {
		base = App.state.target:get_base()
	    }
	)

	filename = App.ui:select_file{
	    title = _("Select Screen Map"),
	    short_desc = _(
		"Select a mapping for translating characters as they " ..
		"appear on your video console screen."
	    ),
	    cancel_desc = _("Return to Utilities Menu"),
	    dir = dir,
	    extension = "scm"
	}
	if filename == "cancel" then
		return false
	end
	filename = dir .. "/" .. filename

	cmds = CmdChain.new()
	cmds:add{
	    cmdline = "${root}${VIDCONTROL} -l ${filename} < /dev/ttyv0",
	    replacements = { filename = filename }
	}
	if cmds:execute() then
		--[[
		snprintf(filename, 256, "/usr/share/syscons/scrnmaps/%s", s);
		snprintf(scrnmapname, 256, filename_noext(basename(filename)));
		config_var_set(rc_conf, "scrnmap", scrnmapname);
		]]--

		return true
	else
		App.ui:inform(_(
		    "Errors occurred; screen map was not successfully set."
		))
	end
end

return {
    name = _("Change Console Settings"),
    effect = function()
	if set_video_font() and set_screen_map() and set_keyboard_map() then
		-- everything was set.
		-- XXX write to rc.conf ?
	end
	return Menu.CONTINUE
    end
}
