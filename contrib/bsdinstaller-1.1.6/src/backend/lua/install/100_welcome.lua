-- $Id: 100_welcome.lua,v 1.7 2005/02/24 23:08:04 cpressey Exp $

require "gettext"

return {
    name = "welcome",
    title = "Welcome",
    action = function(fsm)
	return App.ui:present({
	    id = "begin_install",
    	    name =  _("Begin Installation"),
	    short_desc = _(
		"This experimental application will install %s"			..
		" on one of the hard disk drives attached to this computer. "	..
		"It has been designed to make it easy to install "		..
		"%s in the typical case. "					..
		"If you have special requirements that are not addressed "	..
		"by this installer, or if you have problems using it, you "	..
		"are welcome to install %s manually. "				..
		"To do so select Exit to Live CD, login as root, and follow "	..
		"the instructions given in the file /README ."			..
		"\n\n"								..
		"NOTE! As with any installation process, YOU ARE "		..
		"STRONGLY ENCOURAGED TO BACK UP ANY IMPORTANT DATA ON THIS "	..
		"COMPUTER BEFORE PROCEEDING!",
		App.os.name, App.os.name, App.os.name),
	    long_desc = _(
		"Some situations in which you might not wish to use this "	..
		"installer are:\n\n"						..
		"- you want to install %s onto a "				..
		"logical/extended partition;\n"					..
		"- you want to install %s "					..
		"onto a ``dangerously dedicated'' disk; or\n"			..
		"- you want full and utter control over the install process.",
		App.os.name, App.os.name),

	    actions = {
		{
		    id = "proceed",
		    name = _("Install %s", App.os.name),
		    effect = function()
			return fsm:next()
		    end
		},
		{
		    id = "cancel",
		    name = _("Return to Previous Menu"),
		    effect = function()
			return nil
		    end
		}
	    }
	}).result
    end
}
