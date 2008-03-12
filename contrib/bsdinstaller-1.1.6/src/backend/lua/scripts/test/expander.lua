-- $Id: expander.lua,v 1.1 2004/11/20 19:29:22 cpressey Exp $
-- Test of expander library.

require "expander"

dir_table = {
	root	= "/",
	home	= "/home/me/",
	tmp	= "/var/tmp/",
	conf	= "/var/conf/"
}

cmd_table = {
	gel	= "bin/gel",
	spritz	= "sbin/spritz",
	mousse	= "usr/bin/mousse",
	conf	= "usr/local/bin/conf"
}

e = Expander.new(dir_table, cmd_table)

print(e:expand("${root}${gel}"))
print(e:expand("${tmp}${conf}"))
print(e:expand("${home}${spritz}"))
print(e:expand("${home}${spritz}", { spritz = "usr/local/sbin/cabbage" } ))
print(e:expand("${home}${boo}"))
