-- filesystem.lua
-- $Id: filesystem.lua,v 1.10 2005/03/27 07:21:08 cpressey Exp $
-- Test of dfuibe_lua's filesystem functionalities.

App = require "app"; App.init()
require "mountpoint"

local booltab = { [false] = "NO", [true] = "YES" }

App.start{
    name = "FileSystem/MountPoints test program",
    logfile = "filesystem.log"
}

files = FileSystem.dir("/etc")
for file in files do
	local fullfile = "/etc/" .. files[file]
	local string = fullfile .. "..." ..
	    "  is file? " .. booltab[FileSystem.is_file(fullfile)] ..
	    "  is dir? " .. booltab[FileSystem.is_dir(fullfile)] ..
	    "  is program? " .. booltab[FileSystem.is_program(fullfile)] ..
	    "  size=" .. tostring(FileSystem.size_of(fullfile))
	print(string)
end

path = "/x/y/z"
print("basename of " .. path .. " is " .. FileSystem.basename(path))
print("dirname of  " .. path .. " is " .. FileSystem.dirname(path))

local fh, fname = FileSystem.mkstemp("/tmp/tmp.XXXXXX")
fh:write("hello!\n")
fh:close()
print("Wrote hello! to " .. fname)

local fs_descs = MountPoints.enumerate()
local i, fs_desc
for i, fs_desc in fs_descs do
	print(
	    "mountpoint=", fs_desc.mountpoint,
	    "device=", fs_desc.device,
	    "type=", fs_desc.type
	)
end

App.stop()
