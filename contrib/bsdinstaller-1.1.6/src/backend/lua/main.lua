-- demo/main.lua
-- $Id: main.lua,v 1.19 2005/04/03 20:28:22 cpressey Exp $
-- Main menu for demonstration application for dfuibe_lua.

App = require "app"; App.init()
require "storage"
require "dfui"
require "fsm"
require "menu"
require "storage_ui"
require "target_system"

Pty = require("pty")

GetText = require "gettext"
GetText.set_package("dfuibe_lua")
GetText.set_locale_dir("/usr/local/share/locale")
GetText.init()

App.start{
    name = "Demonstration App",
    logfile = "demo.log",
    ui = DFUI.new{
	transport = "tcp",	-- XXX default from command line options...
	rendezvous = "9999"
    }
}

--
-- Set one of the following options to run this installer in `mock mode':
--
-- App.option.fake_execution = true     -- don't actually execute anything
-- App.option.confirm_execution = true  -- always ask before executing
--
-- You might also wish to change the root from whence things are drawn:
--
-- App.dir.root = "/usr/release/root/"  -- or what have you
--
-- These can be set from the dfuibe_lua command line like so:
--    option.fake_execution=true dir.root=/usr/release/root
--

-- Create a storage-descriptor for the application.
App.state.storage = StorageDescriptor.new()
App.state.storage:survey()

-- put welcome notify-box here

--
-- First let the user configure the important user-interface aspects
-- of their system (language, keyboard/screenmap if on console,
-- internet connection for logging to remote machine over net, etc.)
--
-- These are termed "pre-install tasks" even though that is a slight
-- misnomer (and an unfortunate acronym):
--
App.descend("pit")

Menu.auto{
    name = "Select Task",
    short_desc = "Choose one of the following tasks to perform.",
    continue_constraint = function(result)
	if App.state.do_reboot then
		return Menu.DONE
	else
		return result
	end
    end
}

--
-- If there is a target system mounted, unmount it before leaving.
--
if App.state.target ~= nil and App.state.target:is_mounted() then
	if not App.state.target:unmount() then
		App.ui:inform(
		    _("Warning: subpartitions were not correctly unmounted.")
		)
	end
end

App.stop()

if App.state.do_reboot then
	-- exit with reboot code
	os.exit(5)
else
	os.exit(0)
end
