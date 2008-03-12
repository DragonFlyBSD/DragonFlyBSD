-- $Id: gen_rel_notes.lua,v 1.1 2005/03/03 20:29:03 cpressey Exp $
-- Lua script to generate release notes.
-- Usage: lua gen_rel_notes.lua prev_release_tag this_release_tag
-- e.g.:  lua gen_rel_notes.lua RELENG_1_1_4 RELENG_1_1_5

local cvsdir = "/home/catseye/projects/installer"
local tmpdir = "/tmp"
local tmpfn  = tmpdir .. "/gen_rel_notes.txt"
local old_tag, new_tag = arg[1], arg[2]
local cmd = "cd " .. cvsdir .. " && cvs log -r" .. old_tag .. ":" .. new_tag ..
	    " >" .. tmpfn .. " 2>&1"
os.execute(cmd)
local file = io.open(tmpfn)

local line
local lines, logs

logs = {}
while true do
	lines = {}
	line = file:read("*l")
	if not line then break end
	while not string.find(line, "^===================") do
		line = file:read("*l")
		if not line then break end
		table.insert(lines, line)
	end
	if not line then break end
	table.insert(logs, lines)
end
file:close()

for logno, logtab in logs do
	local found, len, cap
	local old_rev, new_rev
	if string.find(logtab[1], "^cvs log: warning: no revision") then
		logs[logno] = nil
	else
		for k, v in logtab do
			found, len, cap = string.find(logtab[k], "^\t" .. old_tag .. ": (.+)$")
			if found then old_rev = cap end
			found, len, cap = string.find(logtab[k], "^\t" .. new_tag .. ": (.+)$")
			if found then new_rev = cap end
		end
		if new_rev == old_rev then
			logs[logno] = nil
		end
	end
end

for logno, logtab in logs do
	for k, v in logtab do
		print(logno, v)
	end
	print("")
end
