-- $Id: progress.lua,v 1.4 2004/11/16 04:42:38 cpressey Exp $

require "dfui"

c = DFUIConnection.new("tcp", "9999")
c:start()

pr = DFUIProgress.new(c,
    "Fake Progress", "Here is a progress bar that measures nothing", "")

pr:start()

desc = {
    "Cabbage", "Celery", "Garbanzo Beans", "Radishes", "Corn",
    "Iceberg Lettuce", "Asparagus", "Kidney Beans", "Snow Peas", "Broccoli"
}

for i = 1, 10 do
	pr:set_short_desc("Let's pretend we're installing: " .. desc[i])
	if not pr:update() then
		print("user cancelled.")
		break
	end
	os.execute("sleep 1")
	pr:set_amount(i * 10)
end

pr:stop()

c:stop()
