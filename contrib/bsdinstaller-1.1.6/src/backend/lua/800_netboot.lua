-- $Id: 800_netboot.lua,v 1.1 2005/03/27 01:46:09 cpressey Exp $

return {
    name = _("Set up NetBoot Server"),
    short_desc = _("Make this computer a boot server " ..
		   "for other machines on the network"),
    effect = function()
	App.descend("netboot")
	return Menu.CONTINUE
    end
}
