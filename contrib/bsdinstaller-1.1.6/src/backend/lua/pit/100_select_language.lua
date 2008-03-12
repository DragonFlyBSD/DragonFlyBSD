-- $Id: 100_select_language.lua,v 1.3 2005/02/24 23:08:04 cpressey Exp $

require "gettext"

return {
    name = "language",
    title = "Select Language",
    action = function(fsm)
	local actions = {}
	local sel_lang = nil

	-- for i in languages do table.insert(actions, language_action) end
	table.insert(actions, {
	    id = "en",
	    name = _("English")
	})
	
	sel_lang = App.ui:present({
	    id = "select_language",
    	    name =  _("Select Language"),
	    short_desc = _("Please select your preferred language."),
	    role = "menu",
	    actions = actions
	}).action_id

	return fsm:next()
    end
}
