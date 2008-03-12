-- $Id: 450_select_packages.lua,v 1.4 2005/02/24 23:08:04 cpressey Exp $

require "package"
require "storage_ui"

return {
    name = "select_packages",
    title = "Select Packages",
    action = function(fsm)
	local i, j, pkg, regexp

	if not App.state.sel_pkgs then
		local pkg_list = Package.list_all("")
		local def_pkgs = App.load_conf("def_pkgs") or {}

		App.state.sel_pkgs = {}
		for i, pkg in pkg_list do
			App.state.sel_pkgs[pkg] = false -- but present...
			for j, regexp in def_pkgs do
				if string.find(pkg, regexp) then
					App.state.sel_pkgs[pkg] = true
				end
			end
		end
	end

	local ok, sel_pkgs = StorageUI.select_packages{
	    name = _("Select Packages"),
	    short_desc = _("Select the packages you wish to install from " ..
			    "the LiveCD onto the HDD."),
	    checkbox_name = _("Install?"),
	    ok_name = _("Accept these Packages"),
	    cancel_name = _("Return to %s", fsm:prev().title),
	    
	    sel_pkgs = App.state.sel_pkgs
	}

	if ok then
		App.state.sel_pkgs = sel_pkgs
		return fsm:next()
	else
		return fsm:prev()
	end
    end
}
