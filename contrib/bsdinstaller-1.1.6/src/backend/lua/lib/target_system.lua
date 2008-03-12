-- $Id: target_system.lua,v 1.25 2005/03/29 21:04:19 cpressey Exp $

local App = require("app")
local FileName = require("filename")
require "gettext"
require "cmdchain"
require "storage"
require "mountpoint"

--[[--------------]]--
--[[ TargetSystem ]]--
--[[--------------]]--

TargetSystem = {}

--
-- There are three general use cases for this class.
--
-- The first case is when mounting a virgin target system
-- for a fresh install.  In this situation, the needed
-- mountpoint directories (taken from the subpartition descriptors
-- in the partition descriptor) are created on the target system
-- before being mounted:
--
--	local ts = TargetSystem.new(pd, "mnt")
--	if ts:create() then
--		if ts:mount() then
--			...
--			ts:unmount()
--		end
--	end
--
-- The second case is when mounting an existing target system
-- for configuration.  In this situation, the root partition is
-- mounted, then the file /etc/fstab on that partition is parsed,
-- and everything that can reasonably be mounted from that is.
--
--	local ts = TargetSystem.new(pd, "mnt")
--	if ts:probe() then
--		if ts:mount() then
--			...
--			ts:unmount()
--		end
--	end
--
-- The third case is when configuring the booted system, such as
-- when the configurator may be started from the installed system
-- itself.  In this case, no mounting is necessary.  But the target
-- system must know that this is the case, so that it need not
-- e.g. uselessly chroot to itself.
--
--	local ts = TargetSystem.new(pd)
--	if ts:use_current() then
--		...
--	end
--
TargetSystem.new = function(pd, base)
	local ts = {}			-- instance variable
	local fstab = nil		-- our representation of fstab
	local root_is_mounted = false	-- flag: is the root fs mounted?
	local is_mounted = false	-- flag: is everything mounted?
	local using_current = false	-- flag: using booted system?

	--
	-- Private utility helper functions.
	--

	--
	-- Unmount all mountpoints under a given directory.  Recursively unmounts
	-- dependent mountpoints, so that unmount_all'ing /mnt will first unmount
	-- /mnt/usr/local, then /mnt/usr, then /mnt itself.
	--
	-- The third argument is generally not necessary when calling this function;
	-- it is used only when it recursively calls itself.
	--
	local unmount_all_under
	unmount_all_under = function(cmds, dirname, fs_descs)
		local unmount_me = false
		local i

		if not dirname then
			dirname = App.expand("${root}${base}", {
			    base = base
			})
			dirname = FileName.remove_trailing_slash(dirname)
		end

		if not fs_descs then
			fs_descs = MountPoints.enumerate()
		end
	
		for i, fs_desc in fs_descs do
			if fs_desc.mountpoint == dirname then
				unmount_me = true
			end
	
			if string.sub(fs_desc.mountpoint, 1, string.len(dirname)) == dirname and
			   string.len(dirname) < string.len(fs_desc.mountpoint) then
				unmount_all_under(cmds, fs_desc.mountpoint, fs_descs)
			end
		end

		if unmount_me then
			cmds:add({
			    cmdline = "${root}${UMOUNT} ${dirname}",
			    replacements = { dirname = dirname }
			})
		end
	end

	--
	-- Convert the options for swap-backed devices from their
	-- fstab format to command line format.
	--
	local convert_swap_options = function(opts)
		local opt
		local result = ""

		for opt in string.gfind(opts, "[^,]") do
			--
			-- Honour options that begin with -, but
			-- don't bother trying to honour the -C
			-- option, since we can't copy files from
			-- the right place anyway.
			--
			if string.find(opt, "^-[^C]") then
				result = result ..
				    string.gsub(opt, "=", " ") .. " "
			end
		end
		
		return result
	end

	--
	-- Mount this TargetSystem's root filesystem.
	-- Note: this doesn't just queue up commands, it actually does it.
	-- This is necessary for reading /etc/fstab.
	-- Any optimizations will come later...
	--
	local mount_root_filesystem = function(ts)
		local cmds, spd

		if root_is_mounted then
			return false, "Root filesystem is already mounted"
		end

		--
		-- Create a command chain.
		--
		cmds = CmdChain.new()
	
		--
		-- Find the root subpartition of the partition.
		-- It's always the first one, called "a".
		--
		spd = pd:get_subpart_by_letter("a")

		--
		-- If there isn't one, then this partition isn't
		-- correctly formatted.  One possible cause is
		-- an incomplete formatting operation; perhaps the
		-- partition was disklabeled, but never newfs'ed.
		--
		if not spd then
			return false
		end

		--
		-- Ensure that the devices we'll be using exist.
		--
		pd:get_parent():cmds_ensure_dev(cmds)
		pd:cmds_ensure_dev(cmds)
		spd:cmds_ensure_dev(cmds)
	
		--
		-- Make sure nothing is mounted under where we want
		-- to mount this filesystem.
		--
		unmount_all_under(cmds)

		--
		-- Mount the target's root filesystem
		--
		cmds:add({
		    cmdline = "${root}${MOUNT} ${root}dev/${dev} ${root}${base}",
		    replacements = {
			dev = spd:get_device_name(),
			base = base
		    }
		})
		
		--
		-- Do it.
		--
		root_is_mounted = cmds:execute()
		return root_is_mounted
	end

	--
	-- Accessor methods.
	--

	ts.get_part = function(ts)
		return(pd)
	end

	ts.get_base = function(ts)
		return(base)
	end

	ts.is_mounted = function(ts)
		return(is_mounted)
	end

	--
	-- Command-generating methods.
	--

	ts.cmds_set_password = function(ts, cmds, username, password)
		cmds:add({
		    cmdline = "${root}${CHROOT} ${root}${base} " ..
			      "/${PW} usermod ${username} -h 0",
		    replacements = {
		        base = base,
		        username = username
		    },
		    desc = _("Setting password for user `%s'...", username),
		    input = password .. "\n",
		    sensitive = password
		})
	end

	ts.cmds_add_user = function(ts, cmds, tab)

		local add_flag = function(flag, setting)
			if setting ~= nil and setting ~= "" then
				return flag .. " " .. tostring(setting)
			else
				return ""
			end
		end

		local home_skel = ""
		if not tab.home or not FileName.is_dir(tab.home) then
			home_skel = "-m -k /usr/share/skel"
		end

		cmds:add({
		    cmdline = "${root}${CHROOT} ${root}${base} /${PW} useradd " ..
			      "'${username}' ${spec_uid} ${spec_gid} -c \"${gecos}\"" ..
			      "${spec_home} -s ${shell} ${spec_groups} ${home_skel}",
		    replacements = {
		        base = base,
		        username = assert(tab.username),
			gecos = tab.gecos or "Unnamed User",
			shell = tab.shell or "/bin/sh",
		        spec_uid = add_flag("-u", tab.uid),
		        spec_gid = add_flag("-g", tab.group),
		        spec_home = add_flag("-d", tab.home),
			spec_groups = add_flag("-G", tab.groups),
			home_skel = home_skel
		    },
		})

		if tab.password then
			ts:cmds_set_password(cmds, tab.username, tab.password)
		end
	end

	--
	-- Create commands to copy files and directories to the traget system.
	--
	ts.cmds_install_srcs = function(ts, cmds, srclist)
		local i, src, dest, spd

		--
		-- Only bother to copy the mountpoint IF:
		-- o   We have said to copy it at some point
		--     (something in srclist is a prefix of it); and
		-- o   We have not already said to copy it
		--     (it is not a prefix of anything in srclist.)
		--
		local is_valid_mountpoint = function(root, mountpoint)
			local seen_it = false
			local i, src
		
			local starts_with = function(str, prefix)
				return string.sub(str, string.len(prefix)) == prefix
			end
		
			for i, src in ipairs(srclist) do
				if starts_with(mountpoint, root .. src) then
					seen_it = true
				end
				if starts_with(root .. src, mountpoint) then
					return false
				end
			end
			
			return seen_it
		end

		for i, src in ipairs(srclist) do
			--
			-- Create intermediate directories as needed.
			--
			cmds:add{
			    cmdline = "${root}${MKDIR} -p ${root}${base}${src_dir}",
			    replacements = {
				base = base,
				src_dir = FileName.dirname(src)
			    }
			}

			--
			-- If a source by the same name but with the suffix
			-- ".hdd" exists on the installation media, cpdup that
			-- instead.  This is particularly useful with /etc, which
			-- may have significantly different behaviour on the
			-- live CD compared to a standard HDD boot.
			--
			dest = src
			if FileName.is_dir(App.dir.root .. src .. ".hdd") or
			   FileName.is_file(App.dir.root .. src .. ".hdd") then
				src = src .. ".hdd"
			end

			--
			-- Cpdup the chosen source onto the HDD.
			--
			cmds:add({
			    cmdline = "${root}${CPDUP} ${root}${src} ${root}${base}${dest}",
			    replacements = {
				base = base,
				src = src,
				dest = dest
			    },
			    log_mode = CmdChain.LOG_QUIET	-- don't spam log
			})
		end

		--
		-- Now, because cpdup does not cross mount points,
		-- we must copy anything that the user might've made a
		-- seperate mount point for (e.g. /usr/libdata/lint.)
		--
		for spd in pd:get_subparts() do
			--
			-- Only copy files into the subpartition if:
			-- o  It is a regular (i.e.  not swap) subpartition, and
			-- o  A directory exists on the install medium for it
			--
			
			--
			-- This assumes that the subpartition descriptors
			-- have mountpoints associated with them, which should
			-- (nowadays) always be the case.
			--
			if spd:get_fstype() == "4.2BSD" and
			    FileName.is_dir(App.dir.root .. spd:get_mountpoint()) then
				if is_valid_mountpoint(App.dir.root, spd:get_mountpoint()) then
					--
					-- Cpdup the subpartition.
					--
					-- XXX check for .hdd-extended source dirs here, too,
					-- eventually - but for now, /etc.hdd will never be
					-- the kind of tricky sub-mount-within-a-mount-point
					-- that this part of the code is meant to handle.
					--
					cmds:add{
					    cmdline = "${root}${CPDUP} ${root}${mtpt} " ..
						"${root}${base}${mtpt}",
					    replacements = {
					        base = base,
						mtpt = spd:get_mountpoint()
					    },
					    log_mode = CmdChain.LOG_QUIET
					}
				end
			end
		end
	end

	--
	-- Create mountpoint directories on a new system, based on what
	-- the user wants (the subpartition descriptors under the given
	-- partition descriptor) and return a fstab structure describing them.
	--
	ts.create = function(ts)
		local spd, cmds

		--
		-- Mount the target system's root filesystem,
		-- if not already mounted
		--
		if not root_is_mounted then
			if not mount_root_filesystem() then
				return false
			end
		end

		--
		-- Create mount points for later mounting of subpartitions.
		--
		cmds = CmdChain.new()
		fstab = {}
		for spd in pd:get_subparts() do
			local mtpt = spd:get_mountpoint()
			local dev = spd:get_device_name()

			cmds:set_replacements{
				base = base,
				dev = dev,
				mtpt = FileName.remove_leading_slash(mtpt)
			}

			if spd:is_swap() then
				if App.option.enable_crashdumps and
				   spd:get_capacity() >= App.state.store:get_ram() then
					--
					-- Set this subpartition as the dump device.
					--
					cmds:add("${root}${DUMPON} -v ${root}dev/${dev}")
					App.state.crash_device = mnt_dev
				end
				fstab[mtpt] = {
				    device  = "/dev/" .. dev,
				    fstype  = "swap",
				    options = "sw",
				    dump    = 0,
				    pass    = 0
				}
			else
				if spd:get_mountpoint() ~= "/" then
					cmds:add("${root}${MKDIR} -p ${root}${base}${mtpt}")
				end
				fstype = "ufs"
				opts = "rw"
				fstab[mtpt] = {
				    device  = "/dev/" .. dev,
				    fstype  = "ufs",
				    options = "rw",
				    dump    = 2,
				    pass    = 2
				}
				if mtpt == "/" then
					fstab[mtpt].dump = 1
					fstab[mtpt].pass = 1
				end
			end
		end
		return cmds:execute()
	end

	--
	-- Parse the fstab of a mounted target system.
	-- Returns either a table representing the fstab, or
	-- nil plus an error message string.
	--
	-- As a side effect, this function also associates mountpoints
	-- with the subpartition descriptors under the partition
	-- descriptor with which this target system is associated.
	--
	ts.probe = function(ts)
		local fstab_filename, fstab_file, errmsg
		local spd

		--
		-- Mount the target system's root filesystem,
		-- if not already mounted.
		--
		if not root_is_mounted then
			if not mount_root_filesystem() then
				return nil, "Could not mount / of target system."
			end
		end

		--
		-- Open the target system's fstab and parse it.
		--
		fstab_filename = App.expand("${root}${base}etc/fstab", {
		    base = base
		})
		fstab_file, errmsg = io.open(fstab_filename, "r")
		if not fstab_file then
			return nil, "Could not open /etc/fstab of target system."
		end

		fstab = {}
		line = fstab_file:read()
		while line do
			--
			-- Parse the fstab line.
			--
			if string.find(line, "^%s*#") then
				-- comment: skip it
			elseif string.find(line, "^%s*$") then
				-- blank line: skip it
			else
				local found, len, dev, mtpt, fstype, opts, dump, pass =
				    string.find(line, "%s*([^%s]*)%s*([^%s]*)%s*" ..
				      "([^%s]*)%s*([^%s]*)%s*([^%s]*)%s*([^%s]*)")
				if not found then
					App.log("Warning: malformed line in fstab: " ..
					    line)
				else
					fstab[mtpt] = {
					    device  = dev,
					    fstype  = fstype,
					    options = opts,
					    dump    = dump,
					    pass    = pass
					}
					spd = pd:get_subpart_by_device_name(dev)
					if fstype ~= "ufs" then
						-- Don't associate non-ufs
						-- fs's with any mountpoint.
					elseif not spd then
						-- This can happen if e.g.
						-- the user has included a
						-- subpartition from another
						-- drive in their fstab.
					else
						-- Associate mountpoint.
						spd:set_mountpoint(mtpt)
					end
				end
			end
			line = fstab_file:read()
		end
		fstab_file:close()

		return fstab
	end

	ts.use_current = function(ts)
		using_current = true
		base = "/"
		return true
	end

	--
	-- Mount the system on the given partition into the given mount
	-- directory (typically "mnt".)
	--
	ts.mount = function(ts)
		local cmds, i, mtpt, mtpts, fsdesc

		if using_current or is_mounted then
			return true
		end

		if not root_is_mounted or fstab == nil then
			return false
		end

		--
		-- Go through each of the mountpoints in our fstab,
		-- and if it looks like we should, try mount it under base.
		--
	
		mtpts = {}
		for mtpt, fsdesc in fstab do
			table.insert(mtpts, mtpt)
		end
		table.sort(mtpts)

		cmds = CmdChain.new()
		for i, mtpt in mtpts do
			fsdesc = fstab[mtpt]
			if mtpt == "/" then
				-- It's already been mounted by
				-- read_target_fstab() or create_mountpoints.
			elseif string.find(fsdesc.options, "noauto") then
				-- It's optional.  Don't mount it.
			elseif (not string.find(fsdesc.device, "^/dev/") and
				fsdesc.device ~= "swap") then
				-- Device doesn't start with /dev/ and
				-- it isn't 'swap'.  Don't even go near it.
			elseif mtpt == "none" or fsdesc.fstype == "swap" then
				-- Swap partition.  Don't mount it.
			elseif fsdesc.device == "swap" then
				-- It's swap-backed.  mount_mfs it.
				
				cmds:add({
				    cmdline = "${root}${MOUNT_MFS} ${swap_opts} swap ${root}${base}${mtpt}",
				    replacements = {
					swap_opts = convert_swap_options(fsdesc.options),
					base = base,
					mtpt = FileName.remove_leading_slash(mtpt)
				    }
				})
			else
				-- If we got here, it must be normal and valid.
				cmds:set_replacements{
				    dev  = FileName.basename(fsdesc.device),
				    opts = fsdesc.options,	-- XXX this may need further cleaning?
				    base = base,
				    mtpt = FileName.remove_leading_slash(mtpt)
				}
				cmds:add(
				    "cd ${root}dev && ${root}${TEST_DEV} ${dev} || " ..
					"${root}${SH} MAKEDEV ${dev}",
				    "${root}${MOUNT} -o ${opts} ${root}dev/${dev} ${root}${base}${mtpt}"
				)
			end
		end
	
		is_mounted = cmds:execute()
		return is_mounted
	end

	--
	-- Unmount the target system.
	--
	ts.unmount = function(ts)
		if using_current or
		   (not is_mounted and not root_is_mounted) then
			return true
		end
		local cmds = CmdChain.new()
		unmount_all_under(cmds)
		if cmds:execute() then
			is_mounted = false
			root_is_mounted = false
			return true
		else
			return false
		end
	end

	--
	-- 'Constructor' - initialize instance state.
	--

	--
	-- Fix up base.
	--
	base = base or ""
	base = FileName.add_trailing_slash(base)

	return ts
end
