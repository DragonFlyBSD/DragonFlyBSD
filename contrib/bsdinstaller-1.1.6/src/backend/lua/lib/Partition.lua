-- lib/storage.lua
-- $Id: Partition.lua,v 1.76 2005/03/29 21:04:19 cpressey Exp $
-- Storage Descriptors (a la libinstaller) in Lua.

-- BEGIN lib/storage.lua --

local App = require("app")
local FileName = require("filename")
require "cmdchain"
require "bitwise"
require "mountpoint"

--
-- Note: these methods should try to use consistent terminology:
--
-- 'capacity' of an object refers to its capacity in megabytes.
-- 'size' of an object refers to its size in blocks or sectors
--   (which are assumed to be 512 bytes each.)
-- 'capstring' refers to a string which includes a unit suffix:
--   'M' for megabytes
--   'G' for gigabytes
--   '*' to indicate 'use the remaining space on the device'
--

--[[-------------------]]--
--[[ StorageDescriptor ]]--
--[[-------------------]]--

-- This class returns an object which can represent
-- the system's data storage capabilities.

StorageDescriptor = {}
StorageDescriptor.new = function()
	local disk = {}		-- disks in this storage descriptor
	local ram = 0		-- in megabytes
	local sd = {}		-- instance variable

	-- Internal function.
	local next_power_of_two = function(n)
		local i = 1
		n = math.ceil(n)

		while i < n and i >= 1 do
			i = i * 2
		end

		if i > n then
			return i
		else
			return n
		end
	end

	-- Now set up this object's interface functions

	-- Look through `dmesg', `atacontrol list', etc, and
	-- populate disks and ram with values
	-- that are as accurate and readable as possible.
	-- XXX not yet fully implemented.
	sd.survey = function(sd)
		local pty, line
		local disk_name, dd
		local cmd
		local found, len, cap

		cmd = App.expand("${root}${SYSCTL} -n hw.physmem")
		pty = Pty.open(cmd)
		line = pty:readline()
		pty:close()
		
		App.log("`" .. cmd .. "` returned: " .. line)

		ram = next_power_of_two(tonumber(line) / (1024 * 1024))

		cmd = App.expand("${root}${SYSCTL} -n kern.disks")
		pty = Pty.open(cmd)
		line = pty:readline()
		pty:close()

		App.log("`" .. cmd .. "` returned: " .. line)

		for disk_name in string.gfind(line, "%s*(%w+)") do
			if not string.find(disk_name, "^md") then
				disk[disk_name] = DiskDescriptor.new(sd, disk_name)
			end
		end

		for line in io.lines("/var/run/dmesg.boot") do
			for disk_name, dd in disk do
				found, len, cap =
				    string.find(line, "^" .. disk_name .. ":%s*(.*)$")
				if found then
					dd:set_desc(disk_name .. ": " .. cap)
				end
			end
		end

		cmd = App.expand("${root}${ATACONTROL} list")
		pty = Pty.open(cmd)
		line = pty:readline()
		while line do
			for disk_name, dd in disk do
				found, len, cap =
				    string.find(line, "^%s*Master:%s*" ..
				      disk_name .. "%s*(%<.*%>)$")
				if not found then
					found, len, cap =
					    string.find(line, "^%s*Slave:%s*" ..
					      disk_name .. "%s*(%<.*%>)$")
				end
				if found then
					dd:set_desc(disk_name .. ": " .. cap)
				end
			end
			line = pty:readline()
		end
		pty:close()
	end

	-- Refresh our view of the storage connected to the
	-- system, but remember what disk and/or partition
	-- was selected as well.
	sd.resurvey = function(sd, sel_disk, sel_part)
		local sel_disk_name, sel_part_no

		if sel_disk then
			sel_disk_name = sel_disk:get_name()
		end

		if sel_part then
			sel_part_no = sel_part:get_number()
		end

		sd:survey()

		if sel_disk then
			sel_disk = sd:get_disk_by_name(sel_disk_name)
			if not sel_disk then
				-- XXX warn that sel disk was lost!
			end
		end

		if sel_disk and sel_part then
			sel_part = sel_disk:find_part_by_number(sel_part_no)
			if not sel_part then
				-- XXX warn that sel part was lost!
			end
		end

		return sel_disk, sel_part
	end

	-- Return an iterator which yields the next next
	-- DiskDescriptor object in this StorageDescriptor
	-- each time it is called (typically in a for loop.)
	sd.get_disks = function(sd)
		local disk_name, dd
		local list = {}
		local i, n = 0, 0
		
		for disk_name, dd in disk do
			table.insert(list, dd)
			n = n + 1
		end
		
		table.sort(list, function(a, b)
			return a:get_name() < b:get_name()
		end)

		return function()
			if i <= n then
				i = i + 1
				return list[i]
			end
		end
	end

	-- Given the name of a disk, return that disk descriptor,
	-- or nil if no disk by that name was found.
	sd.get_disk_by_name = function(sd, name)
		local dd

		for dd in sd:get_disks() do
			if dd:get_name() == name then
				return dd
			end
		end
		
		return nil
	end

	sd.get_disk_count = function(sd)
		local disk_name, dd
		local n = 0

		for disk_name, dd in disk do
			n = n + 1
		end
		
		return n
	end

	sd.get_ram = function(sd)			-- in megabytes
		return ram
	end

	sd.get_activated_swap = function(sd)		-- in megabytes
		local pty, line
		local swap = 0
		local found, len, devname, amount
		
		pty = Pty.open(App.expand("${root}${SWAPINFO} -k"))
		line = pty:readline()
		while line do
			if not string.find(line, "^Device") then
				found, len, devname, amount =
				    string.find(line, "^([^%s]+)%s+(%d+)")
				swap = swap + tonumber(amount)
			end
			line = pty:readline()
		end
		pty:close()
		
		return math.floor(swap / 1024)
	end

	sd.dump = function(sd)
		local disk_name, dd

		print("*** DUMP of StorageDescriptor ***")
		for disk_name, dd in disk do
			dd:dump()
		end
	end

	return sd
end

--
-- The following global static utility functions in the
-- StorageDescriptor class are really human interface functions;
-- they might be better placed elsewhere.
--

--
-- Take a capstring and return a number indicating size in blocks.
-- If the capstring is "*", the supplied remainder is returned.
-- If the capstring could not be parsed, returns nil.
--
StorageDescriptor.parse_capstring = function(str, remainder)
	if str == "*" then
		return remainder
	else
		local suffix = string.sub(str, -1, -1)
		local body = string.sub(str, 1, string.len(str) - 1)
				
		if suffix == "G" or suffix == "g" then
			return math.floor(tonumber(body) * 1024 * 1024 * 2)
		elseif suffix == "M" or suffix == "m" then
			return math.floor(tonumber(body) * 1024 * 2)
		else
			-- bad suffix
			return nil
		end
	end
end

--
-- Takes a number specifying a size in blocks and
-- convert it to a capstring.
--
StorageDescriptor.format_capstring = function(blocks)
	if blocks >= 1024 * 1024 * 2 then
		return tostring(math.floor(blocks / (1024 * 1024 * 2) * 100) / 100) .. "G"
	else
		return tostring(math.floor(blocks / (1024 * 2) * 100) / 100) .. "M"
	end
end


--[[----------------]]--
--[[ DiskDescriptor ]]--
--[[----------------]]--

DiskDescriptor = {}
DiskDescriptor.new = function(parent, name)
	local dd = {}		-- instance variable
	local part = {}		-- private: partitions on this disk
	local desc = name	-- private: description of disk
	local cyl, head, sec	-- private: geometry of disk
	local touched = false	-- private: whether we formatted it

	-- Set up this object instance's interface functions first:

	dd.get_parent = function(dd)
		return parent
	end

	dd.get_name = function(dd)
		return name
	end

	dd.set_desc = function(dd, new_desc)
		--
		-- Calculate a score for how well this string describes
		-- a disk.  Reject obviously bogus descriptions (usually
		-- erroneously harvested from error messages in dmesg.)
		--
		local calculate_score = function(s)
			local score = 0

			-- In the absence of any good discriminator,
			-- the longest disk description wins.
			score = string.len(s)

			-- Look for clues
			if string.find(s, "%d+MB") then
				score = score + 10
			end
			if string.find(s, "%<.*%>") then
				score = score + 10
			end
			if string.find(s, "%[%d+%/%d+%/%d+%]") then
				score = score + 10
			end

			-- Look for error messages
			if string.find(s, "resetting") then
				score = 0
			end

			return score
		end

		if calculate_score(new_desc) > calculate_score(desc) then
			desc = new_desc
		end
	end

	dd.get_desc = function(dd)
		return desc
	end

	dd.get_geometry = function(dd)
		return cyl, head, sec
	end

	dd.get_device_name = function(dd)
		return name
	end

	dd.get_raw_device_name = function(dd)
		-- XXX depends on operating system
		return name
	end

	-- Return an iterator which yields the next next
	-- PartitionDescriptor object in this DiskDescriptor
	-- each time it is called (typically in a for loop.)
	dd.get_parts = function(dd)
		local i, n = 0, table.getn(part)

		return function()
			if i <= n then
				i = i + 1
				return part[i]
			end
		end
	end

	-- Given the number of a partition, return that
	-- partition descriptor, or nil if not found.
	dd.get_part_by_number = function(dd, number)
		local pd

		for pd in dd:get_parts() do
			if pd:get_number() == number then
				return pd
			end
		end

		return nil
	end

	dd.get_part_count = function(dd)
		return table.getn(part)
	end

	-- return the disk's capacity in megabytes.
	-- this is actually the sum of the capacities of the
	-- partitions on this disk.
	dd.get_capacity = function(dd)
		local pd
		local cap = 0

		for pd in dd:get_parts() do
			cap = cap + pd:get_capacity()
		end
		
		return cap
	end

	-- return the disk's raw size in sectors.
	dd.get_raw_size = function(dd)
		pty = Pty.open(App.expand(
		    "${root}${FDISK} -t -I " ..
		    dd:get_raw_device_name()))
		line = pty:readline()
		while line do
			local found, len, start, size =
			    string.find(line, "start%s*(%d+)%s*,%s*size%s*(%d+)")
			if found then
				pty:close()
				return tonumber(start) + tonumber(size)
			end
			line = pty:readline()
		end
		pty:close()

		return nil
	end

	dd.touch = function(dd)
		touched = true
	end

	dd.has_been_touched = function(dd)
		return touched
	end

	--
	-- Determine whether any subpartition from any partition of this
	-- disk is mounted somewhere in the filesystem.
	--
	dd.is_mounted = function(dd)
		local fs_descs = MountPoints.enumerate()
		local i, fs_desc, dev_name

		dev_name = dd:get_device_name()
		for i, fs_desc in fs_descs do
			if string.find(fs_desc.device, dev_name, 1, true) then
				return true
			end
		end

		return false
	end

	--
	-- Methods to manipulate the contents of this DiskDescriptor.
	--

	dd.clear_parts = function(dd)
		part = {}
	end

	dd.add_part = function(dd, pd)
		part[pd:get_number()] = pd
		-- pd:set_parent(dd)
	end

	--
	-- Methods to add appropriate commands to CmdChains.
	--

	-- Commands to ensure this device exists.
	dd.cmds_ensure_dev = function(dd, cmds)
		cmds:add({
		    cmdline = "cd ${root}dev && ${root}${TEST_DEV} ${dev} || " ..
			      "${root}${SH} MAKEDEV ${dev}",
		    replacements = {
		        dev = FileName.basename(dd:get_device_name())
		    }
		})
	end

	-- Commands to format this disk.
	dd.cmds_format = function(dd, cmds)
		dd:cmds_ensure_dev(cmds)

		--
		-- Currently you need to pass 'yes' to OpenBSD's fdisk to
		-- be able to do these.  (This is a shot in the dark:)
		--
		if App.os.name == "OpenBSD" then
			cmds:add("${root}${ECHO} 'yes\nyes\nyes\n' | " ..
			    "${root}${FDISK} -I " ..
			    dd:get_raw_device_name())
			cmds:add("${root}${ECHO} 'yes\nyes\nyes\n' | " ..
			    "${root}${FDISK} -B " ..
			    dd:get_raw_device_name())
		else
			cmds:add("${root}${FDISK} -I " ..
			    dd:get_raw_device_name())
			cmds:add("${root}${YES} | ${root}${FDISK} -B " ..
			    dd:get_raw_device_name())
		end
	end

	-- Commands to partition this disk.
	dd.cmds_partition = function(dd, cmds)
		local i, pd
		local active_part_no
		local cyl, head, sec = dd:get_geometry()

		dd:cmds_ensure_dev(cmds)

		cmds:add({
		    cmdline = "${root}${ECHO} 'g c${cyl} h${head} s${sec}' >${tmp}new.fdisk",
		    replacements = {
			cyl = cyl,
			head = head,
			sec = sec
		    }
		})

		i = 1
		while i <= 4 do
			local sysid, start, size = 0, 0, 0

			pd = dd:get_part_by_number(i)
			if pd then
				sysid = pd:get_sysid()
				start = pd:get_start()
				size  = pd:get_size()
				if pd:is_active() then
					active_part_no = pd:get_number()
				end
			end

			cmds:add({
			    cmdline = "${root}${ECHO} 'p ${number} ${sysid} ${start} ${size}' >>${tmp}new.fdisk",
			    replacements = {
				number = i,
				sysid = sysid,
				start = start,
				size = size
			    }
			})

			i = i + 1
		end

		if active_part_no then
			cmds:add({
			    cmdline = "${root}${ECHO} 'a ${number}' >>${tmp}new.fdisk",
			    replacements = {
			        number = active_part_no
			    }
			})
		end

		cmds:add("${root}${CAT} ${tmp}new.fdisk")

		App.register_tmpfile("new.fdisk")
	
		--
		-- Execute the fdisk script.
		--
		cmds:add("${root}${FDISK} -v -f ${tmp}new.fdisk " ..
		    dd:get_raw_device_name())
	end

	dd.cmds_install_bootblock = function(dd, cmds, packet_mode)
		local o = " "
		if packet_mode then
			o = "-o packet "
		end
		cmds:add(
		    {
			cmdline = "${root}${BOOT0CFG} -B " ..
			    o .. dd:get_raw_device_name(),
			failure = CmdChain.FAILURE_WARN,
			tag = dd
		    },
		    {
			cmdline = "${root}${BOOT0CFG} -v " ..
			    dd:get_raw_device_name(),
			failure = CmdChain.FAILURE_WARN,
			tag = dd
		    }
		)
	end

	dd.cmds_wipe_start = function(dd, cmds)
		dd:cmds_ensure_dev(cmds)
		cmds:add("${root}${DD} if=${root}dev/zero of=${root}dev/" ..
		    dd:get_raw_device_name() .. " bs=32k count=16")
	end

	dd.dump = function(dd)
		local part_no

		print("\t" .. name .. ": " .. cyl .. "/" .. head .. "/" .. sec .. ": " .. desc)
		for part_no in part do
			part[part_no]:dump()
		end
	end

	-- 'Constructor' - initialize our private state.
	-- Try to find out what we can about ourselves from fdisk.

	local pty, line, found, len

	-- Get the geometry from 'fdisk'.
	pty = Pty.open(App.expand("${root}${FDISK} " .. name))
	line = pty:readline()
	while line and not found do
		found = string.find(line, "^%s*parameters to be used for BIOS")
		line = pty:readline()
	end

	if found then
		found, len, cyl, head, sec =
		    string.find(line, "^%s*cylinders=(%d+)%s*heads=(%d+)%s*" ..
				      "sectors/track=(%d+)")
		cyl = tonumber(cyl)
		head = tonumber(head)
		sec = tonumber(sec)
	end
	pty:close()

	if not found then
		App.log("Warning!  Could not determine geometry of disk " .. name .. "!")
		return nil
	end

	App.log("New Disk: " .. name .. ": " .. cyl .. "/" .. head .. "/" .. sec)

	-- Get the partitions from 'fdisk -s'.
	pty = Pty.open(App.expand("${root}${FDISK} -s " .. name))
	line = pty:readline()  -- geometry - we already have it
	line = pty:readline()	-- headings, just ignore
	line = pty:readline()
	while line do
		local part_no, start, size, sysid, flags
		found, len, part_no, start, size, sysid, flags =
		    string.find(line, "^%s*(%d+):%s*(%d+)%s*(%d+)" ..
				      "%s*0x(%x+)%s*0x(%x+)%s*$")
		if found then
			part_no = tonumber(part_no)
			part[part_no] = PartitionDescriptor.new{
			    parent = dd,
			    number = part_no,
			    start  = tonumber(start),
			    size   = tonumber(size),
			    sysid  = tonumber(sysid, 16),
			    flags  = tonumber(flags, 16)
			}
		end
		line = pty:readline()
	end
	pty:close()

	return dd
end

--[[---------------------]]--
--[[ PartitionDescriptor ]]--
--[[---------------------]]--

PartitionDescriptor = {}
PartitionDescriptor.new = function(params)
	local pd = {}		-- instance variable
	local subpart = {}	-- subpartitions on this partition

	local parent = assert(params.parent)
	local number = assert(params.number)
	local start  = assert(params.start)
	local size   = assert(params.size)
	local sysid  = assert(params.sysid)
	local flags  = params.flags
	if params.active ~= nil then
		if params.active then
			flags = 256
		else
			flags = 0
		end
	end
	assert(type(flags) == "number")

	-- First set up this object's interface functions

	pd.get_parent = function(pd)
		return parent
	end

	pd.get_number = function(pd)
		return number
	end

	pd.get_params = function(pd)
		return start, size, sysid, flags
	end

	pd.get_start = function(pd)
		return start
	end

	pd.get_size = function(pd)
		return size
	end

	pd.get_sysid = function(pd)
		return sysid
	end

	pd.get_flags = function(pd)
		return flags
	end

	-- 'size' is the partition size, in blocks.
	-- return the partition's capacity in megabytes.
	pd.get_capacity = function(pd)
		return math.floor(size / 2048)
	end

	pd.is_active = function(pd)
		return Bitwise.bw_and(flags, 256) == 256
	end
		
	pd.get_desc = function(pd)
		return tostring(number) .. ": " ..
		    tostring(pd:get_capacity()) .. "M (" ..
		    tostring(start) .. "-" .. tostring(start + size) ..
		    ") id=" .. sysid
	end

	pd.get_device_name = function(pd)
		return parent.get_name() .. "s" .. number
	end

	pd.get_raw_device_name = function(pd)
		-- XXX depends on operating system
		return parent.get_name() .. "s" .. number -- .. "c"
	end

	pd.get_activated_swap = function(pd)	-- in megabytes
		local pty, line
		local swap = 0
		local found, len, devname, amount
		
		pty = Pty.open(App.expand("${root}${SWAPINFO} -k"))
		line = pty:readline()
		while line do
			if not string.find(line, "^Device") then
				found, len, devname, amount =
				    string.find(line, "^([^%s]+)%s+(%d+)")
				if string.find(devname, pd:get_device_name()) then
					swap = swap + tonumber(amount)
				end
			end
			line = pty:readline()
		end
		pty:close()
		
		return math.floor(swap / 1024)
	end

	-- Return an iterator which yields the next next
	-- PartitionDescriptor object in this DiskDescriptor
	-- each time it is called (typically in a for loop.)
	pd.get_subparts = function(pd)
		local letter, spd
		local list = {}
		local i, n = 0, 0
		
		for letter, spd in subpart do
			table.insert(list, spd)
		end

		table.sort(list, function(a, b)
			-- not sure why we ever get a nil here, but we do:
			if not a and not b then return false end

			return a:get_letter() < b:get_letter()
		end)

		n = table.getn(list)

		return function()
			if i <= n then
				i = i + 1
				return list[i]
			end
		end
	end
	
	pd.clear_subparts = function(pd)
		subpart = {}
	end
	
	pd.add_subpart = function(pd, spd)
		subpart[spd:get_letter()] = spd
		-- spd:set_parent(pd)
	end

	pd.get_subpart_by_letter = function(pd, letter)
		return subpart[letter]
	end

	pd.get_subpart_by_mountpoint = function(pd, mountpoint)
		local letter, spd
		
		for letter, spd in subpart do
			if spd:get_mountpoint() == mountpoint then
				return spd
			end
		end
		
		return nil
	end

	pd.get_subpart_by_device_name = function(pd, device_name)
		local letter, spd

		-- Strip any leading /dev/ or whatever.
		device_name = FileName.basename(device_name)

		for letter, spd in subpart do
			if spd:get_device_name() == device_name then
				return spd
			end
		end
		
		return nil
	end

	--
	-- Determine whether any subpartition of this
	-- partition is mounted somewhere in the filesystem.
	--
	pd.is_mounted = function(pd)
		local fs_descs = MountPoints.enumerate()
		local i, fs_desc, dev_name

		dev_name = pd:get_device_name()
		for i, fs_desc in fs_descs do
			if string.find(fs_desc.device, dev_name, 1, true) then
				return true
			end
		end

		return false
	end
	
	--
	-- Methods to add appropriate commands to CmdChains.
	--

	-- Commands to ensure this device exists.
	pd.cmds_ensure_dev = function(pd, cmds)
		cmds:add({
		    cmdline = "cd ${root}dev && ${root}${TEST_DEV} ${dev} || " ..
			      "${root}${SH} MAKEDEV ${dev}",
		    replacements = {
		        dev = FileName.basename(pd:get_device_name())
		    }
		})
	end

	-- Commands to format this partition.
	pd.cmds_format = function(pd, cmds)
		pd:cmds_ensure_dev(cmds)

		-- The information in parent NEEDS to be accurate here!
		-- Presumably we just did a survey_storage() recently.
	
		--
		-- Set the slice's sysid to 165.
		--

		local cyl, head, sec = parent:get_geometry()
		local start, size, sysid, flags = pd:get_params()

		cmds:set_replacements{
		    cyl = cyl,
		    head = head,
		    sec = sec,
		    number = pd:get_number(),
		    sysid = 165,
		    start = start,
		    size = size,
		    dev = pd:get_raw_device_name(),
		    parent_dev = parent:get_raw_device_name()
		}

		cmds:add(
		    "${root}${ECHO} 'g c${cyl} h${head} s${sec}' >${tmp}new.fdisk",
		    "${root}${ECHO} 'p ${number} ${sysid} ${start} ${size}' >>${tmp}new.fdisk"
		)

		if pd:is_active() then
			cmds:add("${root}${ECHO} 'a ${number}' >>${tmp}new.fdisk")
		end

		App.register_tmpfile("new.fdisk")
	
		--
		-- Dump the fdisk script to the log for debugging.
		-- Execute the fdisk script
		-- Auto-disklabel the slice.
		-- Remove any old disklabel that might be hanging around.
		--
		cmds:add(
		    "${root}${CAT} ${tmp}new.fdisk",
		    "${root}${FDISK} -v -f ${tmp}new.fdisk ${parent_dev}",
		    "${root}${DISKLABEL} -B -r -w ${dev} auto",
		    "${root}${RM} -f ${tmp}install.disklabel.${parent_dev}"
		)
	end

	pd.cmds_wipe_start = function(pd, cmds)
		pd:cmds_ensure_dev(cmds)
		cmds:add(
		    "${root}${DD} if=${root}dev/zero of=${root}dev/" ..
		    pd:get_raw_device_name() .. " bs=32k count=16"
		)
	end

	pd.cmds_install_bootstrap = function(pd, cmds)
		pd:cmds_ensure_dev(cmds)
		--
		-- NB: one cannot use "/dev/adXsY" here -
		-- it must be in the form "adXsY".
		--
		cmds:add(
		    "${root}${DISKLABEL} -B " ..
		    pd:get_raw_device_name()
		)
		return cmds
	end

	pd.cmds_disklabel = function(pd, cmds)
		-- Disklabel the given partition with the
		-- subpartitions attached to it.

		local num_parts = 8
		if App.os.name == "DragonFly" then
			num_parts = 16
		end

		cmds:set_replacements{
		    part = pd:get_device_name(),
		    num_parts = tostring(num_parts)
		}

		if not FileName.is_file(App.dir.tmp .. "install.disklabel" .. pd:get_device_name()) then
			--
			-- Get a copy of the 'virgin' disklabel.
			-- XXX It might make more sense for this to
			-- happen right after format_slice() instead.
			--
			cmds:add("${root}${DISKLABEL} -r ${part} >${tmp}install.disklabel.${part}")
		end

		--
		-- Weave together a new disklabel out the of the 'virgin'
		-- disklabel, and the user's subpartition choices.
		--

		--
		-- Take everything from the 'virgin' disklabel up until the
		-- '8 or 16 partitions' line, which looks like:
		--
		-- 8 or 16 partitions:
		-- #        size   offset    fstype   [fsize bsize bps/cpg]
		-- c:  2128833        0    unused        0     0       	# (Cyl.    0 - 2111*)
		--
	
		cmds:add(
		    "${root}${AWK} '$2==\"partitions:\" || cut { cut = 1 } !cut { print $0 }' " ..
		      "<${tmp}install.disklabel.${part} >${tmp}install.disklabel",
		    "${root}${ECHO} '${num_parts} partitions:' >>${tmp}install.disklabel",
		    "${root}${ECHO} '#        size   offset    fstype   [fsize bsize bps/cpg]' " ..
		      ">>${tmp}install.disklabel"
		)

		--
		-- Write a line for each subpartition the user wants.
		--

		local spd = nil
		local copied_original = false

		for spd in pd:get_subparts() do
			if spd:get_letter() > "c" and not copied_original then
				--
				-- Copy the 'c' line from the 'virgin' disklabel.
				--
				cmds:add("${root}${GREP} '^  c:' ${tmp}install.disklabel.${part} " ..
					 ">>${tmp}install.disklabel")
				copied_original = true
			end

			cmds:set_replacements{
			    letter = spd:get_letter(),
			    fsize = spd:get_fsize(),
			    bsize = spd:get_bsize()
			}

			if spd:get_letter() == "a" then
				cmds:set_replacements{ offset = "0" }
			else
				cmds:set_replacements{ offset = "*" }
			end
			if spd:get_size() == -1 then
				cmds:set_replacements{ size = "*" }
			else
				cmds:set_replacements{ size = tostring(spd:get_size()) }
			end

			if spd:is_swap() then
				cmds:add("${root}${ECHO} '  ${letter}:\t${size}\t*\tswap' >>${tmp}install.disklabel")
			else
				cmds:add("${root}${ECHO} '  ${letter}:\t${size}\t${offset}\t4.2BSD\t${fsize}\t${bsize}\t99' >>${tmp}install.disklabel")
			end
		end

		if not copied_original then
			--
			-- Copy the 'c' line from the 'virgin' disklabel,
			-- if we haven't yet (less than 2 subpartitions.)
			--
			cmds:add("${root}${GREP} '^  c:' ${tmp}install.disklabel.${part} >>${tmp}install.disklabel")
		end

		App.register_tmpfile("install.disklabel")
	
		--
		-- Label the slice from the disklabel we just wove together.
		--
		-- Then create a snapshot of the disklabel we just created
		-- for debugging inspection in the log.
		--
		cmds:add(
		    "${root}${DISKLABEL} -R -B -r ${part} ${tmp}install.disklabel",
		    "${root}${DISKLABEL} ${part}"
		)
	
		--
		-- Create filesystems on the newly-created subpartitions.
		--
		pd:get_parent():cmds_ensure_dev(cmds)
		pd:cmds_ensure_dev(cmds)

		for spd in pd:get_subparts() do
			if not spd:is_swap() then
				spd:cmds_ensure_dev(cmds)

				if spd:is_softupdated() then
					cmds:add("${root}${NEWFS} -U ${root}dev/" ..
					    spd:get_device_name())
				else
					cmds:add("${root}${NEWFS} ${root}dev/" ..
					    spd:get_device_name())
				end
			end
		end
	end

	pd.cmds_write_fstab = function(pd, cmds, filename)
		-- Write a new fstab for the given partition
		-- to the given filename.

		if not filename then
			filename = "${root}mnt/etc/fstab"
		end

		cmds:set_replacements{
		    header = "# Device\t\tMountpoint\tFStype\tOptions\t\tDump\tPass#",
		    procline = "proc\t\t\t/proc\t\tprocfs\trw\t\t0\t0",
		    device = "???",
		    mountpoint = "???",
		    filename = App.expand(filename)
		}

		cmds:add("${root}${ECHO} '${header}' >${filename}")
	
		for spd in pd:get_subparts() do
			cmds:set_replacements{
			    device = spd:get_device_name(),
			    mountpoint = spd:get_mountpoint()
			}

			if spd:get_mountpoint() == "/" then
				cmds:add("${root}${ECHO} '/dev/${device}\t\t${mountpoint}\t\tufs\trw\t\t1\t1' >>${filename}")
			elseif spd:is_swap() then
				cmds:add("${root}${ECHO} '/dev/${device}\t\tnone\t\tswap\tsw\t\t0\t0' >>${filename}")
			else
				cmds:add("${root}${ECHO} '/dev/${device}\t\t${mountpoint}\t\tufs\trw\t\t2\t2' >>${filename}")
			end
		end
	
		cmds:add("${root}${ECHO} '${procline}' >>${filename}")
	end

	pd.dump = function(pd)
		local letter, spd

		print("\t\tPartition " .. number .. ": " ..
		    start .. "," .. size .. ":" .. sysid .. "/" .. flags)
		for spd in pd:get_subparts() do
			spd:dump()
		end
	end

	App.log("New Partition: " .. number .. ": " ..
	    start .. "," .. size .. ":" .. sysid .. "/" .. flags)

	-- 'Constructor' - initialize this object's state.
	-- If this looks like a BSD slice, try to probe it with
	-- disklabel to get an idea of the subpartitions on it.

	local pty, line, found, len
	local letter, size, offset, fstype, fsize, bsize

	if sysid == 165 then
		pty = Pty.open(App.expand("${root}${DISKLABEL} " .. parent:get_name() ..
		    "s" .. number))
		line = pty:readline()
		found = false
		while line and not found do
			found = string.find(line, "^%d+%s+partitions:")
			line = pty:readline()
		end
		if found then
			while line do
				found, len, letter, size, offset, fstype,
				    fsize, bsize = string.find(line,
				    "^%s*(%a):%s*(%d+)%s*(%d+)%s*([^%s]+)")
				if found then
					fsize, bsize = 0, 0
					if fstype == "4.2BSD" then
						found, len, letter, size,
						    offset, fstype, fsize,
						    bsize = string.find(line,
						    "^%s*(%a):%s*(%d+)%s*" ..
						    "(%d+)%s*([^%s]+)%s*" ..
						    "(%d+)%s*(%d+)")
					end
					subpart[letter] =
					    SubpartitionDescriptor.new{
						parent = pd,
						letter = letter,
						size = size,
						offset = offset,
						fstype = fstype,
						fsize = fsize,
						bsize = bsize
					    }
				end
				line = pty:readline()
			end
		end
			
		pty:close()
	end

	return pd
end

--[[------------------------]]--
--[[ SubpartitionDescriptor ]]--
--[[------------------------]]--

SubpartitionDescriptor = {}
SubpartitionDescriptor.new = function(params)
	local spd = {}		-- instance variable

	local parent = assert(params.parent)
	local letter = assert(params.letter)
	local size   = assert(params.size)
	local offset = assert(params.offset)
	local fstype = assert(params.fstype)
	local fsize  = assert(params.fsize)
	local bsize  = assert(params.bsize)
	local mountpoint = params.mountpoint

	-- Now set up this object's interface functions

	spd.get_parent = function(spd)
		return parent
	end
	
	spd.get_letter = function(spd)
		return letter
	end

	spd.set_mountpoint = function(spd, new_mountpoint)
		mountpoint = new_mountpoint
	end

	spd.get_mountpoint = function(spd)
		return mountpoint
	end

	spd.get_fstype = function(spd)
		return fstype
	end

	spd.get_device_name = function(spd)
		return parent.get_parent().get_name() ..
		    "s" .. parent.get_number() .. letter
	end

	spd.get_raw_device_name = function(spd)
		-- XXX depends on operating system
		return parent.get_parent().get_name() ..
		    "s" .. parent.get_number() .. letter
	end

	spd.get_capacity = function(spd)	-- in megabytes
		return math.floor(size / 2048)
	end

	spd.get_size = function(spd)		-- in sectors
		return size
	end

	spd.get_fsize = function(spd)
		return fsize
	end

	spd.get_bsize = function(spd)
		return bsize
	end

	spd.is_swap = function(spd)
		return fstype == "swap"
	end

	spd.is_softupdated = function(spd)
		-- XXX this should be a property
		return mountpoint ~= "/"
	end

	spd.dump = function(pd)
		print("\t\t\t" .. letter .. ": " .. offset .. "," .. size ..
		      ": " .. fstype .. " -> " .. mountpoint)
	end

	-- Commands to ensure this device exists.
	spd.cmds_ensure_dev = function(spd, cmds)
		cmds:add({
		    cmdline = "cd ${root}dev && ${root}${TEST_DEV} ${dev} || " ..
			      "${root}${SH} MAKEDEV ${dev}",
		    replacements = {
		        dev = FileName.basename(spd:get_device_name())
		    }
		})
	end

	--
	-- Constructor.
	--

	App.log("New Subpartition on " .. parent:get_device_name() .. ": " ..
	    letter .. ": " .. offset .. "," .. size .. ": " .. fstype ..
	    "  F=" .. fsize .. ", B=" .. bsize)

	return spd
end

-- END of lib/storage.lua --
