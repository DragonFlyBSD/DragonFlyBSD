-- $Id: 500_install_packages.lua,v 1.7 2005/04/04 20:38:50 cpressey Exp $

require "cmdchain"
require "package"
require "storage_ui"

local num_keys = function(tab)
	local k, v
	local i = 0
	for k, v in tab do i = i + 1 end
	return i
end

local install_packages = function()
	local base = App.state.target:get_base()
	local pkg_list = Package.list_all("")
	local ok, i, pkg_name, selected
	local sel_pkgs = {}

	for i, pkg_name in pkg_list do
		if not Package.exists(base, pkg_name) then
			sel_pkgs[pkg_name] = false -- but present...
		end
	end

	ok, sel_pkgs = StorageUI.select_packages{
	    name = _("Select Packages"),
	    short_desc = _("Select the packages you wish to install from " ..
			    "the LiveCD onto the HDD."),
	    checkbox_name = _("Install?"),
	    ok_name = _("Install these Packages"),
	    cancel_name = _("Cancel"),

	    sel_pkgs = sel_pkgs
	}

	if ok then
		local cmds = CmdChain.new()
		local pkg_seen = {}
		local pkg_done = {}
		local pkg_extra = ""
		local n, i = 0, 0
		local pr = App.ui:new_progress_bar{
		    title = _("Calculating package dependencies...")
		}

		for pkg_name, selected in sel_pkgs do
			if selected then
				n = n + 1
			end
		end

		pr:start()
		for pkg_name, selected in sel_pkgs do
			if selected then
				Package.cmds_copy(
				    base, cmds, pkg_name, pkg_seen, pkg_done
				)
				i = i + 1
				pr:set_amount((i * 100) / n)
				pr:update()
			end
		end
		pr:stop()

		for pkg_name, selected in pkg_seen do
			if not sel_pkgs[pkg_name] then
				pkg_extra = pkg_extra .. pkg_name .. "\n"
			end
		end

		if pkg_extra ~= "" then
			if not App.ui:confirm(_(
				"The following packages are required to " ..
				"support the packages you selected, and " ..
				"will also be installed:\n\n%s\n" ..
				"Is this acceptable?", pkg_extra
			    )) then
				return
			end
		end

		if cmds:execute() then
			App.ui:inform(_(
			    "%d/%d packages were successfully installed!",
			    num_keys(pkg_done), num_keys(pkg_seen)
			))
		else
			App.ui:inform(_(
			    "Errors occurred while installing packages. " ..
			    "Some packages may not have been " ..
			    "successfully installed."
			))
		end
	end
end

return {
    name = _("Install Packages"),
    effect = function()
	install_packages()
	return Menu.CONTINUE
    end
}
