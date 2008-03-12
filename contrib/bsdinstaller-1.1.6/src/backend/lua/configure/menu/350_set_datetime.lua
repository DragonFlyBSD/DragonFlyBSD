-- $Id: 350_set_datetime.lua,v 1.2 2005/02/24 23:08:03 cpressey Exp $

require "gettext"

local set_datetime = function()
	local cmds, date_tab, form, response

	date_tab = os.date("*t")

	form = {
	    id = "set_datetime",
	    name = _("Set Time and Date"),
	    short_desc = _(
		"Enter the current date and time of day to set " ..
		"this computer's internal timekeeping clock."
	    ),

	    fields = {
		{
		    id = "year",
		    name = _("Year"),
		    short_desc = _("Enter the current year (e.g. `2004')")
		},
		{
		    id = "month",
		    name = _("Month"),
		    short_desc = _("Enter the current month (e.g. `07')")
		},
		{
		    id = "day",
		    name = _("Day"),
		    short_desc = _("Enter the current day of month (e.g. `30')")
		},
		{
		    id = "hour",
		    name = _("Hour"),
		    short_desc = _("Enter the current hour (e.g. `07')")
		},
		{
		    id = "min",
		    name = _("Minute"),
		    short_desc = _("Enter the current minute (e.g. `59')")
		}
	    },

	    datasets = {
		{
		    year = tostring(date_tab.year),
		    month = tostring(date_tab.month),
		    day = tostring(date_tab.day),
		    hour = tostring(date_tab.hour),
		    min = tostring(date_tab.min)
		}
	    },

	    actions = {
		{
		    id = "ok",
		    name = _("OK")
		},
		{
		    id = "cancel",
		    name = _("Cancel")
		}
	    }
	}

	while not done do
		response = App.ui:present(form)
		if response.action_id ~= "ok" then
			return false
		end

		date_tab = {
		    year = tonumber(response.datasets[1].year),
		    month = tonumber(response.datasets[1].month),
		    day = tonumber(response.datasets[1].day),
		    hour = tonumber(response.datasets[1].hour),
		    min = tonumber(response.datasets[1].min)
		}

		--
		-- Validate the given date and time.
		--

		if date_tab.year > 0 and 
		   date_tab.month >= 1 and date_tab.month <= 12 and 
		   date_tab.day >= 1 and date_tab.day <= 31 and 
		   date_tab.hour >= 0 and date_tab.hour <= 23 and 
		   date_tab.min >= 0 and date_tab.min <= 59 then

			cmds = CmdChain.new()
			cmds:add("${root}${DATE} -n " .. string.format(
			    "%04d%02d%02d%02d%02d",
			    date_tab.year,
			    date_tab.month,
			    date_tab.day,
			    date_tab.hour,
			    date_tab.min
			))
			if cmds:execute() then
				App.ui:inform(_(
				    "The time and date have successfully been set."
				))
				done = true
			else
				App.ui:inform(_(
				    "An error occurred while attempting to set " ..
				    "time and date."
				))
			end
		else
			App.ui:inform(_(
			    "Please enter numbers within acceptable ranges " ..
			    "for year, month, day of month, hour, and minute."
			))
		end
	end

	return true
end

return {
    name = _("Set Time and Date"),
    effect = function()
	set_datetime()
	return Menu.CONTINUE
    end
}
