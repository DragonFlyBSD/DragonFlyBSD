-- $Id: 900_reboot.lua,v 1.7 2005/02/24 23:08:04 cpressey Exp $

require "gettext"

return {
    name = "reboot",
    title = "Reboot",
    action = function()
	local response = App.ui:present({
	    id = "reboot",
	    name = _("Reboot"),
	    short_desc = _("This machine is about to be shut down. " ..
	        "After the machine has reached its shutdown state, " ..
	        "you may remove the CD from the CD-ROM drive tray " ..
	        "and press Enter to reboot from the HDD."),
	    role = "confirm",
	    actions = {
	        {
		    id = "ok",
		    name = _("Reboot"),
		},
	        {
		    id = "cancel",
		    name = _("Return to Main Menu"),
		}
	    }
	})

	App.state.do_reboot = (response.action_id == "ok")

	return nil
    end
}
