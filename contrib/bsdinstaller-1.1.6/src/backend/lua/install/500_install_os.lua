-- $Id: 500_install_os.lua,v 1.39 2005/04/05 07:37:20 cpressey Exp $

require "gettext"
require "target_system"

return {
    name = "install_os",
    title = "Install OS",
    action = function(fsm)
	local spd, cmds

	--
	-- Final confirmation.
	--
	if App.ui:present({
	    id = "ready_to_install",
	    name = _("Ready to Install"),
	    short_desc = _(
		"Everything is now ready to install the actual files which "	..
		"comprise the %s operating system "				..
		"on the selected partition of the selected disk.\n\n"		..
		"Note that this process will take quite a while to finish. "	..
		"You may wish to take a break now and come back to the "	..
		"computer in a short while.", App.os.name),
	    actions = {
		{
		    id = "ok",
		    name = _("Begin Installing Files")
		},
		{
		    id = "cancel",
		    name = _("Return to %s", fsm:prev().title)
		}
	    }
	}).action_id ~= "ok" then
		return fsm:prev()
	end

	--
	-- If there is a target system mounted, unmount it before starting.
	--
	if App.state.target ~= nil and App.state.target:is_mounted() then
		if not App.state.target:unmount() then
			App.ui:inform(
			    _("Warning: already-mounted target system could " ..
			      "not be correctly unmounted first."))
			return fsm:current()
		end
	end

	--
	-- Create a command chain.
	--
	cmds = CmdChain.new()

	--
	-- Activate swap, if there is none activated so far.
	--
	if App.state.storage:get_activated_swap() == 0 then
		for spd in App.state.sel_part:get_subparts() do
			if spd:get_fstype() == "swap" then
				cmds:add{
				    cmdline = "${root}${SWAPON} ${root}dev/${dev}",
				    replacements = {
					dev = spd:get_device_name()
				    }
				}
			end
		end
	end

	--
	-- Mount the target system.  This will create the
	-- mountpoint directories the user asked for and
	-- will mount their subpartitions on them.
	--
	App.state.target = TargetSystem.new(App.state.sel_part, "mnt")
	if not App.state.target:create() then
		App.ui:inform(
		    _("Could not create the skeletal target system.")
		)
		return fsm:current()
	end
	if not App.state.target:mount() then
		App.ui:inform(
		    _("Could not mount the skeletal target system.")
		)
		return fsm:current()
	end

	--
	-- Create the commands which will install the chosen directories
	-- onto the target system.
	--
	App.state.target:cmds_install_srcs(cmds, App.load_conf("sources"))

	--
	-- If we were to copy /usr/local and /usr/X11R6 onto the HDD,
	-- *all* packages installed on the LiveCD would be copied to
	-- the HDD.  This may not be what the user wants.  So instead,
	-- create an appropriate filesystem hierarchy under /usr/local
	-- and /usr/X11R6 and populate them with only mandatory packages.
	-- And first, remove the copied-over package database, as it is
	-- inaccurate.
	--
	cmds:add(
	    "${root}${RM} -rf ${root}mnt/var/db/pkg",
	    "${root}${MKDIR} -p ${root}mnt/var/db/pkg",
	    "${root}${CHMOD} 755 ${root}mnt/var/db/pkg",
	    "${root}${MKDIR} -p ${root}mnt/usr/local",
	    "${root}${MTREE} -deU -f ${root}etc/mtree/BSD.local.dist -p ${root}mnt/usr/local",
	    "${root}${MKDIR} -p ${root}mnt/usr/X11R6",
	    "${root}${MTREE} -deU -f ${root}etc/mtree/BSD.x11-4.dist -p ${root}mnt/usr/X11R6"
	)

	--
	-- Create symlinks.
	--

	--
	-- If the user has both /var and /tmp subparitions,
	-- symlink /var/tmp to /tmp.
	--
	if App.state.sel_part:get_subpart_by_mountpoint("/tmp") and
	   App.state.sel_part:get_subpart_by_mountpoint("/var") then
		cmds:add(
		    "${root}${CHMOD} 1777 ${root}mnt/tmp",
		    "${root}${RM} -rf ${root}mnt/var/tmp",
		    "${root}${LN} -s /tmp ${root}mnt/var/tmp"
		)
	end

	--
	-- If the user has /var, but no /tmp,
	-- symlink /tmp to /var/tmp.
	--
	if not App.state.sel_part:get_subpart_by_mountpoint("/tmp") and
	   App.state.sel_part:get_subpart_by_mountpoint("/var") then
		cmds:add(
		    "${root}${RM} -rf ${root}mnt/tmp",
		    "${root}${LN} -s /var/tmp ${root}mnt/tmp"
		)
	end

	--
	-- If the user has /usr, but no /home,
	-- symlink /home to /usr/home.
	--
	if not App.state.sel_part:get_subpart_by_mountpoint("/home") and
	   App.state.sel_part:get_subpart_by_mountpoint("/usr") then
		cmds:add(
		     "${root}${RM} -rf ${root}mnt/home",
		     "${root}${MKDIR} ${root}mnt/usr/home",
		     "${root}${LN} -s /usr/home ${root}mnt/home"
		)
	end

	--
	-- Clean up.  In case some file didn't make it, use rm -f
	--
	if App.os == "DragonFly" then
		cmds:add("${root}${RM} -f ${root}mnt/boot/loader.conf")
	end
	cmds:set_replacements{
	    logfile = App.log_filename,
	    part = App.state.sel_part:get_device_name()
	}
	cmds:add("${root}${RM} -f ${root}mnt/tmp/${logfile}")

	--
	-- Create missing directories.
	--
	cmds:add(
	    "${root}${MKDIR} ${root}mnt/proc",
	    "${root}${MKDIR} ${root}mnt/mnt"
	)

	--
	-- Write the fstab.
	--
	App.state.sel_part:cmds_write_fstab(cmds, "${root}mnt/etc/fstab")

	--
	-- If it was enabled and selected, write the crash device
	-- in the user's new rc.conf file on the HDD.
	--
	if App.state.crash_device ~= nil then
		local cv = ConfigVars.new()

		cv:set("dumpdev", "/dev/" .. App.state.crash_device)
		cv:set("dumpdir", "/var/crash")
		cv:write(App.expander:expand("${root}mnt/etc/rc.conf"), "sh")
	end

	--
	-- Install requested packages.
	--
	if App.state.sel_pkgs ~= nil then
		local pkg_name, selected
		local pkg_seen = {}
		local n, i = 0, 0
		local pr = App.ui:new_progress_bar{
		    title = _("Calculating package dependencies...")
		}

		for pkg_name, selected in App.state.sel_pkgs do
			if selected then
				n = n + 1
			end
		end

		pr:start()
		for pkg_name, selected in App.state.sel_pkgs do
			if selected then
				Package.cmds_copy(
				    "mnt/", cmds, pkg_name, pkg_seen
				)
				i = i + 1
				pr:set_amount((i * 100) / n)
				pr:update()
			end
		end
		pr:stop()
	end

	--
	-- Backup the disklabel and the log.
	--
	cmds:add(
	    "${root}${DISKLABEL} ${part} >${root}mnt/etc/disklabel.${part}",
	    "${root}${CP} ${tmp}${logfile} ${root}mnt/var/log/${logfile}",
	    "${root}${CHMOD} 600 ${root}mnt/var/log/${logfile}"
	)

	--
	-- Do it!
	--
	if cmds:execute() then
		--
		-- If the install went successfully,
		-- make sure once and for all that the disklabel is bootable.
		--
		cmds = CmdChain.new()
		App.state.sel_part:cmds_install_bootstrap(cmds)
		if not cmds:execute() then
			App.ui:inform(
			    _("Failed to make sure that the partition is bootable.")
			)
		end
		return fsm:next()
	else
		App.ui:inform(
		    _("%s was not fully installed.", App.os.name)
		)
		return fsm:current()
	end
    end
}
