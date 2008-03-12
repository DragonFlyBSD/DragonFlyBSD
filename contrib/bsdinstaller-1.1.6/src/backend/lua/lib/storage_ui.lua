-- $Id: storage_ui.lua,v 1.6 2005/04/04 20:50:59 cpressey Exp $

require "gettext"

--[[-----------]]--
--[[ StorageUI ]]--
--[[-----------]]--

StorageUI = {}

-- Function to present a form to the user from which they
-- can select any disk present in the given StorageDescriptor.

StorageUI.select_disk = function(tab)
	local dd
	local disk_actions = {}

	local sd = tab.sd or error("Need a storage descriptor")

	local add_disk_action = function(tdd)
		table.insert(disk_actions,
		    {
			id = tdd:get_name(),
			name = tdd:get_desc(),
			effect = function()
				return tdd
			end
		    }
		)
	end

	for dd in sd:get_disks() do
		add_disk_action(dd)
	end

	table.insert(disk_actions,
	    {
		id = "cancel",
		name = tab.cancel_desc or _("Cancel"),
		effect = function()
		    return nil
		end
	    }
	)

	return App.ui:present({
	    id = tab.id or "select_disk",
	    name = tab.name or _("Select a Disk"),
	    short_desc = tab.short_desc or _("Select a disk."),
	    long_desc = tab.long_desc,
	    actions = disk_actions,
	    role = "menu"
	}).result
end

-- Function to present a form to the user from which they
-- can select any partition present in the given DiskDescriptor.

StorageUI.select_part = function(tab)
	local pd
	local part_actions = {}

	local dd = tab.dd or error("Need a disk descriptor")

	local add_part_action = function(tpd)
		table.insert(part_actions,
		    {
			id = tostring(tpd:get_number()),
			name = tpd:get_desc(),
			effect = function()
				return tpd
			end
		    }
		)
	end

	for pd in dd:get_parts() do
		add_part_action(pd)
	end

	table.insert(part_actions,
	    {
		id = "cancel",
		name = tab.cancel_desc or _("Cancel"),
		effect = function()
		    return nil
		end
	    }
	)

	return App.ui:present({
	    id = tab.id or "select_part",
	    name = tab.name or _("Select a Partition"),
	    short_desc = tab.short_desc or _("Select a partition."),
	    long_desc = tab.long_desc,
	    actions = part_actions,
	    role = "menu"
	}).result
end

StorageUI.select_packages = function(tab)
	local datasets_list = {}
	local pkg, selected, i, dataset

	if not tab.sel_pkgs then
		tab.sel_pkgs = {}
	end
	for pkg, selected in tab.sel_pkgs do
		table.insert(datasets_list, {
		    selected = (selected and "Y") or "N",
		    package = pkg
		})
	end
	table.sort(datasets_list, function(a, b)
		return a.package < b.package
	end)

	local fields_list = {
		{
		    id = "selected",
		    name = tab.checkbox_name or _("Install?"),
		    control = "checkbox"
		},
		{
		    id = "package",
		    name = _("Full Name of Package"),
		    editable = "false"
		}
	}

	local actions_list = {
		{
		    id = "all",
		    name = tab.all_name or _("Select All")
	        },
		{
		    id = "none",
		    name = tab.none_name or _("Select None")
	        },
		{
		    id = "ok",
		    name = tab.ok_name or _("Accept these Packages")
	        },
		{
		    id = "cancel",
		    name = tab.cancel_name or _("Cancel")
	        }
	}

	while true do
		local response = App.ui:present({
		    id = tab.id or "select_packages",
		    name = tab.name or _("Select Packages"),
		    short_desc = tab.short_desc or
			_("Select the packages you wish to install."),
		    long_desc = tab.long_desc,
		    actions = actions_list,
		    fields = fields_list,
		    datasets = datasets_list,
	
		    multiple = "true"
		})

		datasets_list = response.datasets

		if response.action_id == "all" then
			for i, dataset in datasets_list do
				dataset.selected = "Y"
			end
		elseif response.action_id == "none" then
			for i, dataset in datasets_list do
				dataset.selected = "N"
			end
		else
			local pkg_selected = {}
			for i, dataset in datasets_list do
				pkg_selected[dataset.package] = (dataset.selected == "Y")
			end
			return response.action_id == "ok", pkg_selected
		end
	end
end
