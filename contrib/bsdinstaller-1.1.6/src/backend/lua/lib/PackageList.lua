-- lib/package.lua
-- $Id: PackageList.lua,v 1.13 2005/04/04 18:59:20 cpressey Exp $
-- Installer package functions written in Lua.

-- BEGIN lib/package.lua --

local POSIX = require("posix")
local FileName = require("filename")
local App = require("app")

require "cmdchain"

--[[---------]]--
--[[ Package ]]--
--[[---------]]--

-- This 'class' currently only contains static global methods
-- for dealing with packages.  There is no 'package object.'

Package = {}

--
-- Determine whether a package is installed on a HDD.
--
Package.exists = function(base, pkg_name)
	return FileName.is_dir(
	    App.expand("${root}${base}var/db/pkg/${pkg_name}", {
	        base = base,
		pkg_name = pkg_name
	    })
	)
end

Package.list_all = function(base)
	local i, filename, list
	local dir = POSIX.dir(
	    App.expand("${root}${base}var/db/pkg", {
		base = base
	    })
	)

	list = {}
	for i, filename in ipairs(dir) do
		if filename ~= "." and filename ~= ".." then
			table.insert(list, filename)
		end
	end

	return list
end

--
-- Methods which construct command-chains.
--

--
-- Delete all packages from a given base.
--
Package.cmds_clean = function(base, cmds)
	cmds:add({
	    cmdline = "${root}${CHROOT} ${root}${base} /${PKG_DELETE} '*'",
	    replacements = { base = base }
	})
end

--
-- Create commands to copy a package from the installation media onto
-- the target system.
--
-- This function returns the number of packages that will be copied
-- by the commands it has created, or nil if an error occurs.
--
Package.cmds_copy = function(base, cmds, pkg_name, pkg_seen, pkg_done)
	local pty, rpkg_name, line, pkg_suffix
	local depcount = 0
	pkg_seen = pkg_seen or {}
	pkg_done = pkg_done or {}

	--
	-- Get all the packages that this package depends on, and
	-- recursively copy them first, if they're not already there,
	-- and if we've not already seen them.
	--
	-- It woulld be nice if we could send this command through a command
	-- chain  so that we could get an accurate idea of what is being
	-- run and so that it will be logged.  But unfortunately that's
	-- not feasible, since this function is building another command
	-- chain for later use.  So we use a pty.
	--
	pty = Pty.open(App.expand("${root}${PKG_INFO} -r ") .. pkg_name)
	if not pty then
		return nil
	end

	line = pty:readline()
	while line do
		--
		-- Only look at lines that begin with 'Dependency:'.
		--
		local found, len, rpkg_name =
		    string.find(line, "^Dependency:%s*([^%s]+)")
		if found and not Package.exists(base, rpkg_name) then
			local subcount = Package.cmds_copy(
			    base, cmds, rpkg_name, pkg_seen, pkg_done
			)
			if subcount == nil then
				pty:close()
				return nil
			end
			depcount = depcount + subcount
		end
		line = pty:readline()
	end
	pty:close()

	pkg_suffix = "tgz"
	if App.os.name == "FreeBSD" then
		pkg_suffix = "tbz"
	end

	if not Package.exists(base, pkg_name) and not pkg_seen[pkg_name] then
		pkg_seen[pkg_name] = true
		depcount = depcount + 1
		cmds:set_replacements{
		    base       = base,
		    pkg_name   = pkg_name,
		    pkg_suffix = pkg_suffix
		}
		cmds:add(
		    "${root}${PKG_CREATE} -b ${pkg_name} ${root}${base}tmp/${pkg_name}.${pkg_suffix}",
		    {
		        cmdline = "${root}${CHROOT} ${root}${base} " ..
			          "/${PKG_ADD} /tmp/${pkg_name}.${pkg_suffix}",
			tag = pkg_name,
			on_executed = function(cmd, result, output)
			    if result == 0 then
				    pkg_done[cmd.tag] = true
			    end
			end
		    },
		    "${root}${RM} ${root}${base}tmp/${pkg_name}.${pkg_suffix}"
		)
	end

	return depcount
end

--
-- Remove a package from a target system.
--
-- This function returns the number of packages that will be removed
-- by the commands it has created, or nil if an error occurs.
--
Package.cmds_remove = function(base, cmds, pkg_name, pkg_seen, pkg_done)
	local pty, line
	local command, rpkg_name
	local depcount = 0
	local seen_required_by = false
	pkg_seen = pkg_seen or {}
	pkg_done = pkg_done or {}

	--
	-- Get all the packages that this package depends on, and
	-- recursively delete them.
	--
	pty = Pty.open(App.expand(
	    "${root}${CHROOT} ${root}${base} /${PKG_INFO} -R ${pkg_name}", {
		base = base,
		pkg_name = pkg_name
	    }
	))
	if not pty then
		return nil
	end

	line = pty:readline()
	while line do
		--
		-- Only look at lines that follow the "Required by:" line.
		--
		if seen_required_by then
			found, len, rpkg_name =
			    string.find(line, "^%s*([^%s]+)")
			if found and Package.exists(base, rpkg_name) then
				local subcount = Package.cmds_remove(
				    base, cmds, rpkg_name, pkg_seen, pkg_done
				)
				if subcount == nil then
					pty:close()
					return nil
				end
				depcount = depcount + subcount
			end
		else
			if string.find(line, "^Required by:") then
				seen_required_by = true
			end
		end
		line = pty:readline()
	end
	pty:close()

	if Package.exists(base, pkg_name) and not pkg_seen[pkg_name] then
		pkg_seen[pkg_name] = true
		depcount = depcount + 1
		cmds:add({
		    cmdline = "${root}${CHROOT} ${root}${base} /${PKG_DELETE} ${pkg_name}",
		    replacements = {
			base = base,
			pkg_name = pkg_name
		    },
		    tag = pkg_name,
		    on_executed = function(cmd, result, output)
			if result == 0 then
				pkg_done[cmd.tag] = true
			end
		    end
		})
	end
	
	return depcount
end

-- END of lib/package.lua --
