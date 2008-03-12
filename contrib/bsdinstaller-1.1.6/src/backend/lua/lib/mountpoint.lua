-- $Id: mountpoint.lua,v 1.1 2005/03/27 07:21:08 cpressey Exp $

MountPoints = {}

MountPoints.enumerate = function()
	local tab = {}
	local pty, line, found, len
	local device, mtpt, fstype, opts, dump, pass
	local retval

	pty = Pty.open(App.expand("${root}${MOUNT} -p"))
	if not pty then
		return nil, "could not open pty to mount"
	end

	line = pty:readline()
	found = true
	while line and found do
		found, len, device, mtpt, fstype, opts, dump, pass = string.find(line,
		    "^([^%s]+)%s+([^%s]+)%s+([^%s]+)%s+([^%s]+)%s+(%d+)%s+(%d+)"
		)
		if found then
			table.insert(tab, {
			    device = device,
			    mountpoint = mtpt,
			    type = fstype
			})
		end
		line = pty:readline()
	end

	retval = pty:close()
	if retval ~= 0 then
		return nil, "mount failed with return code " .. tostring(retval)
	end

	return tab
end
