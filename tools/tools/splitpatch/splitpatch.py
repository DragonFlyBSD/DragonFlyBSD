#!/usr/local/bin/python
# $DragonFly: src/tools/tools/splitpatch/splitpatch.py,v 1.1 2005/01/10 22:20:27 joerg Exp $

"""Split a patch file into one patch for each file."""

__copyright__ = """
Copyright (c) 2004 Joerg Sonnenberger.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

  * Neither the name of the authors nor the names of there
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""


import os.path

def directory_save(filename, patch, suffix = None, root = None, forceful = False):
	"""
	Saves patch into filename.suffix relative to root.
	"""
	if suffix is None:
		suffix = ".patch"
	if root is None:
		root = ""
	output_name  = os.path.join(root, "%s%s" % (filename.replace('/',','), suffix))
	
	if os.path.exists(output_name) and not forceful:
		raise IOError, 'file exists'
	f = open(output_name,"w")
	f.write(patch)
	f.close()

def splitpatch(source, output = directory_save, quiet = False):
	"""
	Split the patch in source into independent pieces
	and call output on the result with the guessed filename
	and the patch itself.
	
	source has to provide an iterator like file objects.
	"""
	diff_line = { " ": True, "+": True, "-": True, "@": True, "!": True, '*': True }
	buf = []
	filename = None
	for line in source:
		if not filename:
			if line.startswith('***'):
				# context diff
				filename = line.split()[1]
			elif line.startswith('+++'):
				# unified diff
				filename = line.split()[1]

			if filename and not quiet:
				print "Found patch for %s" % filename

			buf.append(line)
		elif diff_line.get(line[0]):
			# valid line for a patch
			buf.append(line)
		else:
			# invalid line for a patch, write out current buffer
			output(filename, "".join(buf))

			filename = None
			buf = []

	if filename:
		output(filename, "".join(buf))

def main():
	from optparse import OptionParser
	import sys
	parser = OptionParser("usage: %prog [-q] [-f] [-s suffix] [input]")
	parser.add_option("-q", "--quiet", action="store_true", dest="quiet", help="do not print names of the subpatches")
	parser.add_option("-f", "--force", action="store_true", dest="force", help="overwrite existing patches")
	parser.add_option("-s", "--suffix", type="string", dest="suffix", help="use SUFFIX instead of .patch for the created patches")
	parser.add_option("-d", "--directory", type="string", dest="directory", help="create patches in DIRECTORY")
	(options, args) = parser.parse_args()
	if len(args) > 1:
		parser.error("incorrect number of arguments")
	if args:
		source = open(args[0])
	else:
		source = sys.stdin
	splitpatch(source, lambda filename, patch: directory_save(filename, patch, forceful = options.force,
				suffix = options.suffix, root = options.directory), quiet = options.quiet)

if __name__ == '__main__':
	main()
