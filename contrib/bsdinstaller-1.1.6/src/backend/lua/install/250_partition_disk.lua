-- $Id: 250_partition_disk.lua,v 1.21 2005/03/31 12:35:50 den Exp $

require "gettext"

local datasets_list = nil

local name_to_sysid_map = {
    ["DragonFly/FreeBSD"] =	165,
    ["OpenBSD"] =		166,
    ["NetBSD"] =		169,
    ["MS-DOS"] =		15,
    ["Linux"] =			131,
    ["Plan9"] =                 57
}

local options_list = {}
local sysid_to_name_map = {}
local k, v
for k, v in name_to_sysid_map do
	table.insert(options_list, k)
	sysid_to_name_map[v] = k
end

local populate_from_disk = function(dd)
	local pd
	local list = {}

	local toyn = function(bool)
		if bool then
			return "Y"
		else
			return "N"
		end
	end

	for pd in dd:get_parts() do
		table.insert(list, {
			capstring = StorageDescriptor.format_capstring(pd:get_size()),
			sysid = sysid_to_name_map[pd:get_sysid()] or
				tostring(pd:get_sysid()),
			active = toyn(pd:is_active())
		})
	end

	return list
end

local populate_one_big_partition = function(dd)
	return {
		{
			capstring = "*",
			sysid     = "165",
			active    = "Y"
		}
	}
end

local edit_partitions = function(fsm)
	if not datasets_list then
		datasets_list = populate_from_disk(App.state.sel_disk)
	end

	local fields_list = {
		{
		    id = "capstring",
		    name = _("Capacity")
		},
		{
		    id = "sysid",
		    name = _("Partition Type"),
		    options = options_list,
		    editable = "false"
		},
		{
		    id = "active",
		    name = _("Active?"),
		    control = "checkbox"
		}
	}

	local actions_list = {
		{
		    id = "ok",
		    name = _("Accept and Create"),
	        },
		{
		    id = "cancel",
		    name = _("Return to %s", fsm:prev().title),
	        }
	}

	local response = App.ui:present({
	    id = "edit_partitions",
	    name = _("Edit Partitions"),
	    short_desc = _("Select the partitions (also known "		..
		"as `slices' in BSD tradition) you want to "		..
		"have on this disk.\n\n"				..
		"For Capacity, use 'M' to indicate megabytes, 'G' to "	..
		"indicate gigabytes, or a single '*' to indicate "	..
		"'use the remaining space on the disk'."),
	    special = "bsdinstaller_edit_partitions",
	    minimum_width = "64",

	    actions = actions_list,
	    fields = fields_list,
	    datasets = datasets_list,

	    multiple = "true",
	    extensible = "true"
	})

	-- remember these subpartition selections in case we come back here.
	datasets_list = response.datasets

	return response.action_id == "ok"
end

local check_datasets = function(dd, list)
	local i, dataset
	local size
	local disk_size = dd:get_raw_size()
	local used_size = 60	-- initial offset
	local wildcard_size = false

	for i, dataset in list do
		if (name_to_sysid_map[dataset.sysid] or tonumber(dataset.sysid)) == 0 then
			App.ui:inform(_(
			    "'%s' is not a recognized partition type. " ..
			    "Please use a numeric identifier if you "	..
			    "wish to use an unlisted partition type.",
			    dataset.sysid
			))
			return false
		end

		if dataset.capstring == "*" then
			if wildcard_size then
				App.ui:inform(_(
				    "Only one partition may have a " ..
				    "capacity of '*'."
				))
				return false
			end
			wildcard_size = true
		end

		size = StorageDescriptor.parse_capstring(dataset.capstring, 0)
		if not size then
			App.ui:inform(_(
			    "Capacity must either end in 'M' "		..
			    "for megabytes, 'G' for gigabytes, "	..
			    "or be '*' to indicate 'use all "		..
			    "remaining space.'"
			))
			return false
		end

		used_size = used_size + size
	end

	if used_size > disk_size then
		if not App.ui:confirm(_(
		    "WARNING: The total number of sectors needed "	..
		    "for the requested partitions (%d) exceeds the "	..
		    "number of sectors available on the disk (%d) "	..
		    "by %d sectors (%s.)\n\n"				..
		    "This is an invalid configuration; we "		..
		    "recommend shrinking the size of one or "		..
		    "more partitions before proceeding.\n\n"		..
		    "Proceed anyway?",
		    used_size, disk_size, used_size - disk_size,
		    StorageDescriptor.format_capstring(used_size - disk_size))) then
			return false
		end
	end

	if used_size < disk_size - App.state.max_waste and
	   not wildcard_size then
		if not App.ui:confirm(_(
		    "Note: the total number of sectors needed "		..
		    "for the requested partitions (%d) does not make "	..
		    "full use of the number of sectors available "	..
		    "on the disk (%d.)  There will be %d unused "	..
		    "sectors (%s.)\n\n"					..
		    "You may wish to expand one or more partitions "	..
		    "before proceeding.\n\n"				..
		    "Proceed anyway?",
		    used_size, disk_size, disk_size - used_size,
		    StorageDescriptor.format_capstring(disk_size - used_size))) then
			return false
		end
	end

	return true
end

--
-- This assumes check_datasets has already been called.
--
local create_partitions_from_datasets = function(dd, list)
	local i, dataset
	local part_no = 1
	local offset = 60
	local disk_size = dd:get_raw_size()
	local used_size = offset
	local size
	local sysid

	dd:clear_parts()

	for i, dataset in list do
		used_size = used_size +
		    StorageDescriptor.parse_capstring(dataset.capstring, 0)
	end

	for i, dataset in list do
		size = StorageDescriptor.parse_capstring(dataset.capstring,
		    disk_size - used_size)

		dd:add_part(PartitionDescriptor.new{
		    parent = dd,
		    number = part_no,
		    start  = offset,
		    size   = size,
		    sysid  = name_to_sysid_map[dataset.sysid] or tonumber(dataset.sysid),
		    active = (dataset.active == "Y")
		})

		offset = offset + size
		part_no = part_no + 1
	end

	return true
end

local do_edit_partitions = function(fsm)
	if edit_partitions(fsm) then
		local cmds = CmdChain.new()

		if check_datasets(App.state.sel_disk, datasets_list) and
		   create_partitions_from_datasets(
		    App.state.sel_disk, datasets_list) then

			App.state.sel_disk:cmds_partition(cmds)
			cmds:execute()

			-- refresh our knowledge of the storage
			App.state.sel_disk, App.state.sel_part =
			    App.state.storage:resurvey(
				App.state.sel_disk, App.state.sel_part
			    )

			-- Mark this disk as having been 'touched'
			-- (modified destructively, i.e. partitioned)
			-- by us.
			if App.state.sel_disk then
				App.state.sel_disk:touch()
			end

			return fsm:next()
		else
			return fsm:current()
		end
	else
		return fsm:prev()
	end
end

return {
    name = "partition_disk",
    title = "Partition Disk",
    action = function(fsm)
	local choices = {}

	if App.state.sel_disk:has_been_touched() then
		return do_edit_partitions(fsm)
	end

	if App.state.sel_disk:get_part_count() == 0 then
		App.ui:inform(_(
		    "No valid partitions were found on this disk. "	..
		    "You will have to create at least one in which "	..
		    "to install %s.",
		    App.os.name
		))
		return do_edit_partitions(fsm)
	end

	choices[_("Format and Partition Disk")] = function()
		return do_edit_partitions(fsm)
	end

	choices[_("Skip this Step")] = function()
		return fsm:next()
	end

	choices[_("Return to %s", fsm:prev().title)] = function()
		return fsm:prev()
	end

	return App.ui:select(_(
	    "You may now format and partition this disk if you desire."		..
	    "\n\n"								..
	    "If this is a brand new disk, you should do this. If you "		..
	    "would like to place multiple operating systems on this disk, "	..
	    "you should create multiple partitions, one for each operating "	..
	    "system."								..
	    "\n\n"								..
	    "If this disk already has operating systems on it that you wish "	..
	    "to keep, you should NOT do this, and should skip this step."	..
	    "\n\n"								..
	    "Format and partition this disk?"),
	    choices
	)()
    end
}
