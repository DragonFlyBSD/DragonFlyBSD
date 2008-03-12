-- $Id: 400_select_subparts.lua,v 1.24 2005/03/29 13:12:43 den Exp $

local expert_mode = false

local datasets_list = nil

local fillout_missing_expert_values = function()
	local i, size

	for i in datasets_list do
		local dataset = datasets_list[i]

		if not dataset.softupdates and
		   not dataset.fsize and not dataset.bsize then
			if dataset.mountpoint == "/" then
				dataset.softupdates = "N"
			else
				dataset.softupdates = "Y"
			end

			size = StorageDescriptor.parse_capstring(dataset.capstring, -1)
			if size and size < (1024 * 1024 * 1024) then
				dataset.fsize = "1024"
				dataset.bsize = "8192"
			else
				dataset.fsize = "2048"
				dataset.bsize = "16384"
			end
		end
	end
end

local warn_subpartition_selections = function(pd)
	local omit = ""
	local consequences = ""

	if not pd:get_subpart_by_mountpoint("/var") then
		omit = omit .. "/var "
		consequences = consequences ..
		    _("%s will be a plain dir in %s\n", "/var", "/")
	end

	if not pd:get_subpart_by_mountpoint("/usr") then
		omit = omit .. "/usr "
		consequences = consequences ..
		    _("%s will be a plain dir in %s\n", "/usr", "/")
	end

	if not pd:get_subpart_by_mountpoint("/tmp") then
		omit = omit .. "/tmp "
		consequences = consequences ..
		    _("%s will be symlinked to %s\n", "/tmp", "/var/tmp")
	end

	if not pd:get_subpart_by_mountpoint("/home") then
		omit = omit .. "/home "
		consequences = consequences ..
		    _("%s will be symlinked to %s\n", "/home", "/usr/home")
	end

	if string.len(omit) > 0 then
		local choices = {}
		
		choices[_("Omit Subpartition(s)")] = true
		choices[_("Return to Create Subpartitions")] = false

		return App.ui:select(_(
		    "You have elected to not have the following "	..
		    "subpartition(s):\n\n%s\n\n"			..
		    "The ramifications of these subpartition(s) being "	..
		    "missing will be:\n\n%s\n"				..
		    "Is this really what you want to do?",
		    omit, consequences), choices)
	else
		return true
	end
end

local validate_subpart_descriptors = function(pd)
	local spd, k, v
	local part_size = pd:get_size()
	local used_size = 0
	local min_size = {}

	-- XXX this should come from a config file.
	for k, v in {
	    ["/"]	=  "70M",
	    ["/var"]	=   "8M",
	    ["/usr"]	= "174M"
	} do
		min_size[k] = StorageDescriptor.parse_capstring(v, 0) or 0
	end

	--
	-- If the user didn't select a /usr partition, / is going to
	-- have to hold all that stuff - so make sure it's big enough.
	--
	if not pd:get_subpart_by_mountpoint("/usr") then
		min_size["/"] = min_size["/"] + min_size["/usr"]
	end

	for spd in pd:get_subparts() do
		local spd_size = spd:get_size()
		local mtpt = spd:get_mountpoint()
		local min_mt_size = min_size[mtpt]

		used_size = used_size + spd_size

		if min_mt_size and spd_size < min_mt_size then
			if not App.ui:confirm(_(
			    "WARNING: the %s subpartition should "	..
			    "be at least %s in size or you will "	..
			    "risk running out of space during "		..
			    "the installation.\n\n"			..
			    "Proceed anyway?",
			    mtpt,
			    StorageDescriptor.format_capstring(min_mt_size))) then
				return false
			end
		end
	end

	if used_size > part_size then
		if not App.ui:confirm(_(
		    "WARNING: The total number of sectors needed "	..
		    "for the requested subpartitions (%d) exceeds the "	..
		    "number of sectors available in the partition (%d) " ..
		    "by %d sectors (%s.)\n\n"				..
		    "This is an invalid configuration; we "		..
		    "recommend shrinking the size of one or "		..
		    "more subpartitions before proceeding.\n\n"		..
		    "Proceed anyway?",
		    used_size, part_size, used_size - part_size,
		    StorageDescriptor.format_capstring(used_size - part_size))) then
			return false
		end
	end

	if used_size < part_size - App.state.max_waste then
		if not App.ui:confirm(_(
		    "Note: the total capacity required "	..
		    "for the requested subpartitions (%s) does not make "	..
		    "full use of the capacity available in the "	..
		    "partition (%s.)  %d sectors (%s) of space will go " ..
		    "unused.\n\n"					..
		    "You may wish to expand one or more subpartitions "	..
		    "before proceeding.\n\n"				..
		    "Proceed anyway?",
		    StorageDescriptor.format_capstring(used_size),
		    StorageDescriptor.format_capstring(part_size),
		    part_size - used_size,
		    StorageDescriptor.format_capstring(part_size - used_size))) then
			return false
		end
	end

	if App.option.enable_crashdumps then
		local num_swap_subparts = 0
		local num_dumponable = 0

		for spd in pd:get_subparts() do
			if spd:is_swap() then
				num_swap_subparts = num_swap_subparts + 1
				if spd:get_capacity() >= App.state.store:get_ram() then
					num_dumponable = num_dumponable + 1
				end
			end
		end
		
		if num_swap_subparts > 0 and num_dumponable == 0 then
			if not App.ui:confirm(_(
			    "Note: none of the swap subpartitions that "	..
			    "you have selected are large enough to hold "	..
			    "the contents of memory, and thus cannot be "	..
			    "used to hold a crash dump (an image of the "	..
			    "computers' memory at the time of failure.) "	..
			    "Because this complicates troubleshooting, "	..
			    "we recommend that you increase the size of "	..
			    "one of your swap subpartitions.\n\n"		..
			    "Proceed anyway?",
			    mtpt, min_cap)) then
				return false
			end
		end
	end

	return warn_subpartition_selections(pd)
end

--
-- Take a list of tables representing the user's choices and
-- create a matching set of subpartition descriptors under
-- the given partition description from them.  In the process,
-- the desired subpartitions are checked for validity.
--
local create_subpart_descriptors = function(pd, list)
	local i, letter, dataset
	local size, offset, fstype
	local total_size = 0
	local wildcard_size = false

	pd:clear_subparts()

	offset = 0
	for i, dataset in list do
		if dataset.capstring == "*" then
			if wildcard_size then
				App.ui:inform(_(
				    "Only one subpartition may have " ..
				    "a capacity of '*'."
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
		total_size = total_size + size
	end

	offset = 0
	for i, letter in ipairs({"a", "b", "d", "e", "f", "g", "h", "i",
				 "j", "k", "l", "m", "n", "o", "p" }) do
		if i > table.getn(list) then break end
		dataset = list[i]

		size = StorageDescriptor.parse_capstring(dataset.capstring,
		    pd:get_size() - total_size)

		if dataset.mountpoint == "swap" then
			fstype = "swap"
		else
			fstype = "4.2BSD"
		end

		pd:add_subpart(SubpartitionDescriptor.new{
		    parent = pd,
		    letter = letter,
		    size   = size,
		    offset = offset,
		    fstype = fstype,
		    fsize  = tonumber(dataset.fsize),
		    bsize  = tonumber(dataset.bsize),
		    mountpoint = dataset.mountpoint
		})

		offset = offset + size
	end

	return validate_subpart_descriptors(pd)
end

return {
    name = "select_subparts",
    title = "Select Subpartitions",
    action = function(fsm)
	local part_no, pd
	local part_actions = {}
	local i, letter

	if not datasets_list then
		datasets_list = App.load_conf("mountpoints")(
		    App.state.sel_part:get_capacity(),
		    App.state.storage:get_ram()
		)
	end

	local fields_list = {
		{
		    id = "mountpoint",
		    name = _("Mountpoint")
		},
		{
		    id = "capstring",
		    name = _("Capacity")
		}
	}

	local actions_list = {
		{
		    id = "ok",
		    name = _("Accept and Create"),
		    effect = function()
			return fsm:next()
		    end
	        },
		{
		    id = "cancel",
		    name = _("Return to %s", fsm:prev().title),
		    effect = function()
			return fsm:prev()
		    end
	        }
	}

	if expert_mode then
		table.insert(fields_list,
		    {
			id = "softupdates",
			name = _("Softupdates?"),
			control = "checkbox"
		    }
		)
		table.insert(fields_list,
		    {
			id = "fsize",
			name = _("Frag Size")
		    }
		)
		table.insert(fields_list,
		    {
			id = "bsize",
			name = _("Block Size")
		    }
		)

		table.insert(actions_list,
		    {
			id = "switch",
			name = _("Switch to Normal Mode"),
			effect = function()
				expert_mode = not expert_mode
				return fsm:current()
			end
		    }
		)
	else
		table.insert(actions_list,
		    {
			id = "switch",
			name = _("Switch to Expert Mode"),
			effect = function()
				expert_mode = not expert_mode
				return fsm:current()
			end
		    }
		)
	end

	local response = App.ui:present({
	    id = "select_subpartitions",
	    name = _("Select Subpartitions"),
	    short_desc = _("Set up the subpartitions (also known "	..
		"as just `partitions' in BSD tradition) you want to "	..
		"have on this primary partition.\n\n"			..
		"For Capacity, use 'M' to indicate megabytes, 'G' to "	..
		"indicate gigabytes, or a single '*' to indicate "	..
		"'use the remaining space on the primary partition'."),
	    long_desc = _("Subpartitions further divide a primary partition for " ..
		"use with %s.  Some reasons you may want "		..
		"a set of subpartitions are:\n\n"			..
		"- you want to restrict how much data can be written "	..
		"to certain parts of the primary partition, to quell "	..
		"denial-of-service attacks; and\n"			..
		"- you want to speed up access to data on the disk.",
		App.os.name),
	    special = "bsdinstaller_create_subpartitions",
	    minimum_width = "64",

	    actions = actions_list,
	    fields = fields_list,
	    datasets = datasets_list,

	    multiple = "true",
	    extensible = "true"
	})

	-- remember these subpartition selections in case we come back here.
	datasets_list = response.datasets
	fillout_missing_expert_values()

	if response.action_id == "ok" then
		if create_subpart_descriptors(App.state.sel_part, datasets_list) then
			local cmds = CmdChain.new()

			App.state.sel_part:cmds_disklabel(cmds)
			cmds:execute()
		else
			return fsm:current()
		end
	end

	return response.result
    end
}
