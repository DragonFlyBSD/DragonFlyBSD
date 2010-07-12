#!/usr/bin/python

#
# Usage: fileDiffer <afile> <bfile> <list of disk changes>
# 

# LUKS 
# quick regression test suite
# Tests LUKS images for changes at certain disk offsets
#
# Does fast python code has to look ugly or is it just me?

import sys

class changes:
	pass

def parseArgs(args):
	aFileName = args[1]
	bFileName = args[2]
	changelist = []
	args[0:3] = []
	for i in args:
		mychanges = changes();
		if i.startswith('A'):
			mychanges.mode = 'ALLOWED'
		if i.startswith('R'):
			mychanges.mode = 'REQUIRED'
			mychanges.strictness = 'RANDOM'
		if i.startswith('S'):
			mychanges.mode = 'REQUIRED'
			mychanges.strictness = 'SEMANTIC'
		
		dashIndex = i.find('-')
		if dashIndex == -1:			
			mychanges.starts = int(i[1:])
			mychanges.ends = mychanges.starts
		else:
			mychanges.starts = int(i[1:dashIndex])
			mychanges.ends = int(i[dashIndex+1:])
		mychanges.miss = 0
		changelist.append(mychanges)
	mychanges = changes();
	mychanges.starts = 0
#	mychanges.ends will be fixed later
	mychanges.mode = 'FORBIDDEN'	
	changelist.append(mychanges)
	return [aFileName, bFileName, changelist]

def mode(i):
	for c in changelist:
		if i >= c.starts and i<=c.ends:
			return c
def cleanchanges(i):
	newchangelist=[]	
	for c in changelist:
		if i <= c.starts or i <= c.ends:
			newchangelist.append(c)
	return newchangelist

[aFileName, bFileName, changelist] = parseArgs(sys.argv)

aFile = open(aFileName,'r')
bFile = open(bFileName,'r')

aString = aFile.read()
bString = bFile.read()

if len(aString) != len(bString): 
	sys.exit("Mismatch different file sizes")

fileLen = len(aString)
fileLen10th = fileLen/10

# Create a catch all entry
changelist[-1].ends = fileLen

print "Changes list: (FORBIDDEN default)"
print "start\tend\tmode\t\tstrictness"
for i in changelist:
	if i.mode == 'REQUIRED':
		print "%d\t%d\t%s\t%s" % (i.starts, i.ends, i.mode, i.strictness)
	else:
		print "%d\t%d\t%s" % (i.starts, i.ends, i.mode)


filepos = 0
fileLen10thC = 0
print "[..........]"
sys.stdout.write("[")
sys.stdout.flush()

modeNotTrivial = 1
while filepos < fileLen:

	if modeNotTrivial == 1:
		c = mode(filepos)
#	print (filepos, c.mode)
	if c.mode == 'REQUIRED':
		if aString[filepos] == bString[filepos]:
			c.miss = c.miss + 1
	else:
		if aString[filepos] != bString[filepos] and c.mode != 'ALLOWED':
			sys.exit("Mismatch at %d: change forbidden" % filepos)

	# Do some maintaince, print progress bar, and clean changelist
	#
	# Maintaining two counters appears to be faster than modulo operation
	if fileLen10thC == fileLen10th:
		fileLen10thC = 0
		sys.stdout.write(".")
		sys.stdout.flush()
		changelist = cleanchanges(filepos)
		if len(changelist) == 1:
			modeNotTrivial = 0
	filepos = filepos + 1
	fileLen10thC = fileLen10thC + 1

for c in changelist:
	if c.mode == 'REQUIRED':
		if c.strictness == 'SEMANTIC' and c.miss == (c.ends-c.starts+1):
			sys.exit("Mismatch: not even a single change in region %d-%d." % (c.starts, c.ends))
   	        # This is not correct. We should do a statistical test
	        # of the sampled data against the hypothetical distribution
	        # of collision. Chi-Square Test.
		if c.strictness == 'RANDOM' and c.miss == (c.ends-c.starts+1):
			sys.exit("Mismatch: not even a single change in region %d-%d." % (c.starts, c.ends))

print ".] - everything ok"
