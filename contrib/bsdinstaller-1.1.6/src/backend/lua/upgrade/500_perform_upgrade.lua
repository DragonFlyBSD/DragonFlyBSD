-- $Id: 500_perform_upgrade.lua,v 1.1 2005/03/27 23:19:29 cpressey Exp $

--
-- Perform an upgrade by copying files from the root of the installation
-- medium to the target system.
--
return {
    name = "perform_upgrade",
    title = "Perform Upgrade",
    action = function(fsm)
	local cmds

	assert(App.state.target)
	cmds = CmdChain.new()
	cmds:set_replacements{
	    base = App.state.target:get_base()
	}

	--
	-- First, we must record which files on the installed system have
	-- the 'noschg' flag set.
	--
	App.register_tmpfile("schg.txt")
	cmds:add("${root}${FIND} ${root}${base} -flags -schg, >${tmp}schg.txt")

	--
	-- Next, we take away the schg flag from these files, so that we
	-- can overwrite them.
	--
	cmds:add("${root}${XARGS} -t ${root}${CHFLAGS} noschg <${tmp}schg.txt")

	--
	-- Add commands to copy sources (files and directories) now.
	--
	App.state.target:cmds_install_srcs(cmds, App.load_conf("sources"))

	--
	-- Before we are done, we must use chflags to restore the flags
	-- of files which we 'noschg'ed so that we could upgrade them.
	--
	cmds:add("${root}${XARGS} -t ${root}${CHFLAGS} schg <${tmp}schg.txt")
	
	--
	-- Finally: do it.
	--
	if cmds:execute() then
		App.ui:inform(
		    _("Target system was successfully upgraded!")
		)
	else
		App.ui:inform(
		    _("Target system was not successfully upgraded.")
		)
	end

	return fsm:next()
    end
}
