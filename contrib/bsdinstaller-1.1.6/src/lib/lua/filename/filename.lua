-- $Id: filename.lua,v 1.1 2005/03/29 20:58:31 cpressey Exp $

module("filename")

local POSIX = require("posix")

--[[----------]]--
--[[ FileName ]]--
--[[----------]]--

--
-- Package of routines for manipulating filenames.
-- Also contains convenience functions for querying the
-- status of files in the filesystem named by those filenames.
--

FileName = {}

--
-- Add a trailing slash to a pathname, if needed.
--
FileName.add_trailing_slash = function(path)
	if string.sub(path, -1) ~= "/" then
		return path .. "/"
	else
		return path
	end
end

--
-- Remove any leading slash from a pathname.
--
FileName.remove_leading_slash = function(path)
	if string.sub(path, 1, 1) == "/" then
		return string.sub(path, 2)
	else
		return path
	end
end

--
-- Remove the trailing slash of a pathname, if present.
--
FileName.remove_trailing_slash = function(path)
	if string.sub(path, -1) == "/" then
		return string.sub(path, 1, string.len(path) - 1)
	else
		return path
	end
end

--
-- Pure Lua version of dirname.
--
FileName.dirname = function(path)
	while true do
		if path == "" or
		   string.sub(path, -1) == "/" or
		   string.sub(path, -2) == "/." or
		   string.sub(path, -3) == "/.." or
		   (string.sub(path, -1) == "." and
		    string.len(path) == 1) or
		   (string.sub(path, -2) == ".." and
		    string.len(path) == 2) then
			break
		end
		path = string.sub(path, 1, -2)
	end
	if path == "" then
		path = "."
	end
	if string.sub(path, -1) ~= "/" then
		path = path .. "/"
	end

	return path
end

--
-- Pure Lua version of basename.
--
FileName.basename = function(path)
	local i = string.len(path)

	while string.sub(path, i, i) == "/" and i > 0 do
		path = string.sub(path, 1, i - 1)
		i = i - 1
	end
	while i > 0 do
		if string.sub(path, i, i) == "/" then
			break
		end
		i = i - 1
	end
	if i > 0 then
		path = string.sub(path, i + 1, -1)
	end
	if path == "" then
		path = "/"
	end

	return path
end

--
-- Query file status in the underlying file system.
-- If the given file is the thing the test is asking for, return 'true'.
-- If it's not that type of thing, but it does exist, return 'false'.
-- If it doesn't even exist, return 'nil'.
--

FileName.is_dir = function(path)
	local stat = POSIX.stat(path)

	if not stat then
		return nil
	else
		return stat.type == "directory"
	end
end

FileName.is_file = function(path)
	local stat = POSIX.stat(path)

	if not stat then
		return nil
	else
		return stat.type == "regular"
	end
end

FileName.is_program = function(path)
	local stat = POSIX.stat(path)

	if not stat then
		return nil
	else
		return stat.type == "regular" and
		       string.sub(stat.mode, 9, 9) == "x"
	end
end

return FileName
