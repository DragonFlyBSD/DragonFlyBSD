-- $Id: 800_finished.lua,v 1.8 2005/02/24 23:08:04 cpressey Exp $

require "gettext"

return {
    name = "finished",
    title = "Finished",
    action = function(fsm)
	return App.ui:present({
	    id = "finished_install",
	    name = _("%s is now installed", App.os.name),
	    short_desc = _("Congratulations, %s is now installed "	 ..
			    "on your hard drive! You may now do one "	 ..
			    "of three things: you can perform some "	 ..
			    "initial configuration of this system, you " ..
			    "can reboot to test out your new "		 ..
			    "installation, or you can go back to the "	 ..
			    "main menu and select other actions to "	 ..
			    "perform.",
			    App.os.name),
	    actions = {
		{
		    id = "configure",
		    name = _("Configure"),
		    short_desc = _("Configure the system that was just installed"),
		    effect = function()
		        App.descend("../configure/menu")
			return nil
		    end
		},
		{
		    id = "reboot",
		    name = _("Reboot"),
		    short_desc = _("Reboot this computer"),
		    effect = function()
			return fsm:next()
		    end
		},
		{
		    id = "cancel",
		    name = _("Return to Main Menu"),
		    short_desc = _("Return to Main Menu"),
		    effect = function()
			return nil
		    end
		}
	    }
	}).result
    end
}
