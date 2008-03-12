-- $Id: dfui.lua,v 1.1 2005/02/08 21:16:17 cpressey Exp $

require "app"
require "dfui"

App.start({
    name = "Test application",
    logfile = "testapp.log",
    ui = DFUI.new{
	transport = "tcp",
	rendezvous = "9999"
    }
})

App.ui:present{
    id = "typical_form",
    name = "Typical Form",
    short_desc = "This is a typical form.",
    fields = {
        {
	    id = "name",
	    name = "Name"
	},
        {
	    id = "type",
	    name = "Type",
	    options = {
	        "First", "Second", "Third"
	    }
	}
    },
    datasets = {
        {
	    name = "Charles",
	    type = "First"
	}
    }
    actions = {
        {
	    id = "ok", name = "OK"
	}
    }
}

App.stop()
