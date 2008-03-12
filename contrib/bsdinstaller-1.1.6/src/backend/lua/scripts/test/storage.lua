-- storage.lua
-- $Id: storage.lua,v 1.3 2004/10/30 05:50:25 cpressey Exp $
-- Test of the storage library.

require("storage")

Storage.survey()

print("System memory: " .. Storage.ram .. "MB")
print("# of disks: " .. table.getn(Storage.disk))

for disk_no, disk in Storage.disk do
	print("Disk #" .. disk_no .. ":")
	print("  Name: " .. disk.name)
	print("  Description: " .. disk.desc)
	print("  # of partitions: " .. table.getn(disk.slice))
	for slice_no, slice in disk.slice do
		print("  Partition #" .. slice_no .. ":")
		print("    Start: " .. slice.start)
		print("    Size: " .. slice.size)
		print("    Type: " .. slice.sysid)
		print("    Flags: " .. slice.flags)
	end
end
