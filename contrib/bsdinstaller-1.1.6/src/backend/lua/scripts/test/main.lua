-- $Id: main.lua,v 1.17 2004/11/17 01:00:21 cpressey Exp $
--  Lua LibDFUI Test Backend

require "dfui"
require "gettext"

function simple_menu_test()
    r = c:present({
	    id = "test",
	    name = _("Test Form"),
	    minimum_width = "78",
	    short_desc = _("This is a test form.  Enter your birthdate in the fields provided below."),
	    actions = {
		{
		    id = "ok",
		    name = _("OK"),
		    short_desc = _("Accept and Close"),
		    effect = function()
		        print(_("You selected 'OK'!"))
			return "this-is-your-result:ok"
		    end
		},
		{
		    id = "cancel",
		    name = _("Cancel"),
		    short_desc = _("Decline and Revert"),
		    effect = function()
		        print(_("You selected 'Cancel'!"))
			return "this-is-your-result:cancel"
		    end
		}
	    },
	    fields = {
		    { id = "year", name = _("Year"), short_desc = _("Enter your year of birth") },
		    { id = "month", name = _("Month"), short_desc = _("Enter your month of birth") },
		    { id = "day", name = _("Day"), short_desc = _("Enter your day of birth"),
		      obscured = "true" }
	    },
	    datasets = {
		    { year = "1967", month = "04", day = "23" }
	    }
    })
    print("c:present()=", r)
    print("r.form_id=", r.form_id)
    print("r.action_id=", r.action_id)
    print("r.result=", r.result)
    print("r.datasets=", r.datasets)
    for i = 1, table.getn(r.datasets) do
	print("", i)
	for k,v in r.datasets[i] do
	    print("", "", k, v)
	end
    end
end

function typical_confirmation()
    r = c:present({
	    id = "confirmation",
	    name = _("Typical Confirmation"),
	    minimum_width = "78",
	    short_desc = _("Typical Confirmation Form"),
	    actions = {
		{
		    id = "ok",
		    name = _("OK"),
		    short_desc = _("Typical Confirmation Form displayed"),
		    effect = function()
		        print(_("Typical Confirmation Form closed!"))
		    end
		}
	    }
    })
    print("c:present()=", r)
    print("r.form_id=", r.form_id)
    print("r.action_id=", r.action_id)
end

--[[ MAIN ]]--

print("DFUIConnection=", DFUIConnection)
c = DFUIConnection.new("tcp", "9999")
print("c=", c)
r = c:start()
print("c:start()=", r)

current_action=""

done = false
while not done do
	r = c:present({
		id = "libdfui_test",
		name = _("LibDFUI Test Form"),
		short_desc = _("This form will help you test LibDFUI"),
		actions = {
			{ id = "simple_menu_test", name = _("Simple"), short_desc = _("Simple Menu Test") },
			{ id = "typical_confirmation", name = _("Typical Confirmation"), short_desc = _("Typical Confirmation Form")}, 
			{ id = "exit", name = _("Exit"), short_desc = _("Exit") }
		},
	})
	print("r=", r)
	for k,v in r do
		print(k,"=",v)
	end
	if r.action_id == "simple_menu_test" then simple_menu_test() end
	if r.action_id == "typical_confirmation" then typical_confirmation() end
	if r.action_id == "exit" then done = true end
end

-- This will be done automatically when c is garbage-collected.
-- However we can do it manually here as well.
r = c:stop()
print("c:stop()=", r)

