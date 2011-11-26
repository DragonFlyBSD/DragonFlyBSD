#! /usr/bin/perl -w
# -*- Perl -*-
# Copyright (C) 1989-2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008,
#               2009
# Free Software Foundation, Inc.
#      Written by James Clark (jjc@jclark.com)
#
# This file is part of groff.
#
# groff is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# groff is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

use strict;

@afmtodit.tables@

my $prog = $0;
$prog =~ s@.*/@@;

my $groff_sys_fontdir = "@FONTDIR@";

use Getopt::Std;
getopts('a:cd:e:f:i:kmnsvx');

our ($opt_a, $opt_c, $opt_d, $opt_e, $opt_f, $opt_i,
     $opt_k, $opt_m, $opt_n, $opt_s, $opt_v, $opt_x);

if ($opt_v) {
    print "GNU afmtodit (groff) version @VERSION@\n";
    exit 0;
}

if ($#ARGV != 2) {
    die "usage: $prog [-ckmnsvx] [-a angle] [-d DESC] [-e encoding]\n" .
	"       [-f name] [-i n] afmfile mapfile font\n";
}

my $afm = $ARGV[0];
my $map = $ARGV[1];
my $font = $ARGV[2];
my $desc = $opt_d || "DESC";
my $sys_map = $groff_sys_fontdir . "/devps/generate/" . $map;
my $sys_desc = $groff_sys_fontdir . "/devps/" . $desc;

# read the afm file

my $psname;
my ($notice, $version, $fullname, $familyname, @comments); 
my $italic_angle = 0;
my (@kern1, @kern2, @kernx);
my (%italic_correction, %left_italic_correction);
my %subscript_correction;
# my %ligs
my %ligatures;
my (@encoding, %in_encoding);
my (%width, %height, %depth);
my (%left_side_bearing, %right_side_bearing);

open(AFM, $afm) || die "$prog: can't open \`$ARGV[0]': $!\n";

while (<AFM>) {
    chomp;
    s/\x0D$//;
    my @field = split(' ');
    next if $#field < 0;
    if ($field[0] eq "FontName") {
	$psname = $field[1];
	if($opt_f) {
	    $psname = $opt_f;
	}
    }
    elsif($field[0] eq "Notice") {
	$notice = $_;
    }
    elsif($field[0] eq "Version") {
	$version = $_;
    }
    elsif($field[0] eq "FullName") {
	$fullname = $_;
    }
    elsif($field[0] eq "FamilyName") {
	$familyname = $_;
    }
    elsif($field[0] eq "Comment") {
	push(@comments, $_);
    }
    elsif($field[0] eq "ItalicAngle") {
	$italic_angle = -$field[1];
    }
    elsif ($field[0] eq "KPX") {
	if ($#field == 3) {
	    push(@kern1, $field[1]);
	    push(@kern2, $field[2]);
	    push(@kernx, $field[3]);
	}
    }
    elsif ($field[0] eq "italicCorrection") {
	$italic_correction{$field[1]} = $field[2];
    }
    elsif ($field[0] eq "leftItalicCorrection") {
	$left_italic_correction{$field[1]} = $field[2];
    }
    elsif ($field[0] eq "subscriptCorrection") {
	$subscript_correction{$field[1]} = $field[2];
    }
    elsif ($field[0] eq "StartCharMetrics") {
	while (<AFM>) {
	    @field = split(' ');
	    next if $#field < 0;
	    last if ($field[0] eq "EndCharMetrics");
	    if ($field[0] eq "C") {
		my $w;
		my $wx = 0;
		my $n = "";
#		%ligs = ();
		my $lly = 0;
		my $ury = 0;
		my $llx = 0;
		my $urx = 0;
		my $c = $field[1];
		my $i = 2;
		while ($i <= $#field) {
		    if ($field[$i] eq "WX") {
			$w = $field[$i + 1];
			$i += 2;
		    }
		    elsif ($field[$i] eq "N") {
			$n = $field[$i + 1];
			$i += 2;
		    }
		    elsif ($field[$i] eq "B") {
			$llx = $field[$i + 1];
			$lly = $field[$i + 2];
			$urx = $field[$i + 3];
			$ury = $field[$i + 4];
			$i += 5;
		    }
#		    elsif ($field[$i] eq "L") {
#			$ligs{$field[$i + 2]} = $field[$i + 1];
#			$i += 3;
#		    }
		    else {
			while ($i <= $#field && $field[$i] ne ";") {
			    $i++;
			}
			$i++;
		    }
		}
		if (!$opt_e && $c != -1) {
		    $encoding[$c] = $n;
		    $in_encoding{$n} = 1;
		}
		$width{$n} = $w;
		$height{$n} = $ury;
		$depth{$n} = -$lly;
		$left_side_bearing{$n} = -$llx;
		$right_side_bearing{$n} = $urx - $w;
#		while ((my $lig, my $glyph2) = each %ligs) {
#		    $ligatures{$lig} = $n . " " . $glyph2;
#		}
	    }
	}
    }
}
close(AFM);

# read the DESC file

my ($sizescale, $resolution, $unitwidth);
$sizescale = 1;

open(DESC, $desc) || open(DESC, $sys_desc) ||
    die "$prog: can't open \`$desc' or \`$sys_desc': $!\n";
while (<DESC>) {
    next if /^#/;
    chop;
    my @field = split(' ');
    next if $#field < 0;
    last if $field[0] eq "charset";
    if ($field[0] eq "res") {
	$resolution = $field[1];
    }
    elsif ($field[0] eq "unitwidth") {
	$unitwidth = $field[1];
    }
    elsif ($field[0] eq "sizescale") {
	$sizescale = $field[1];
    }
}
close(DESC);

if ($opt_e) {
    # read the encoding file

    my $sys_opt_e = $groff_sys_fontdir . "/devps/" . $opt_e;
    open(ENCODING, $opt_e) || open(ENCODING, $sys_opt_e) ||
	die "$prog: can't open \`$opt_e' or \`$sys_opt_e': $!\n";
    while (<ENCODING>) {
	next if /^#/;
	chop;
	my @field = split(' ');
	next if $#field < 0;
	if ($#field == 1) {
	    if ($field[1] >= 0 && defined $width{$field[0]}) {
		$encoding[$field[1]] = $field[0];
		$in_encoding{$field[0]} = 1;
	    }
	}
    }
    close(ENCODING);
}

# read the map file

my (%nmap, %map);

open(MAP, $map) || open(MAP, $sys_map) ||
    die "$prog: can't open \`$map' or \`$sys_map': $!\n";
while (<MAP>) {
    next if /^#/;
    chop;
    my @field = split(' ');
    next if $#field < 0;
    if ($#field == 1) {
	if ($field[1] eq "space") {
	    # The PostScript character "space" is automatically mapped
	    # to the groff character "space"; this is for grops.
	    warn "you are not allowed to map to " .
		 "the groff character \`space'";
	}
	elsif ($field[0] eq "space") {
	    warn "you are not allowed to map " .
		 "the PostScript character \`space'";
	}
	else {
	    $nmap{$field[0]} += 0;
	    $map{$field[0], $nmap{$field[0]}} = $field[1];
	    $nmap{$field[0]} += 1;

	    # There is more than one way to make a PS glyph name;
	    # let us try Unicode names with both `uni' and `u' prefixes.
	    my $utmp = $AGL_to_unicode{$field[0]};
	    if (defined $utmp && $utmp =~ /^[0-9A-F]{4}$/) {
		foreach my $unicodepsname ("uni" . $utmp, "u" . $utmp) {
		    $nmap{$unicodepsname} += 0;
		    $map{$unicodepsname, $nmap{$unicodepsname}} = $field[1];
		    $nmap{$unicodepsname} += 1;
		}
	    }
	}
    }
}
close(MAP);

$italic_angle = $opt_a if $opt_a;


if (!$opt_x) {
    my %mapped;
    my $i = ($#encoding > 256) ? ($#encoding + 1) : 256;
    while (my $ch = each %width) {
	# add unencoded characters
	if (!$in_encoding{$ch}) {
	    $encoding[$i] = $ch;
	    $i++;
	}
	if ($nmap{$ch}) {
	    for (my $j = 0; $j < $nmap{$ch}; $j++) {
		if (defined $mapped{$map{$ch, $j}}) {
		    warn "both $mapped{$map{$ch, $j}} and $ch " .
			 "map to $map{$ch, $j}";
		}
		else {
		    $mapped{$map{$ch, $j}} = $ch;
		}
	    }
	}
	else {
	    my $u = "";		# the resulting groff glyph name
	    my $ucomp = "";	# Unicode string before decomposition
	    my $utmp = "";	# temporary value
	    my $component = "";
	    my $nv = 0;

	    # Step 1:
	    #   Drop all characters from the glyph name starting with the
	    #   first occurrence of a period (U+002E FULL STOP), if any.
	    #   ?? We avoid mapping of glyphs with periods, since they are
	    #   likely to be variant glyphs, leading to a `many ps glyphs --
	    #   one groff glyph' conflict.
	    #
	    #   If multiple glyphs in the font represent the same character
	    #   in the Unicode standard, as do `A' and `A.swash', for example,
	    #   they can be differentiated by using the same base name with
	    #   different suffixes.  This suffix (the part of glyph name that
	    #   follows the first period) does not participate in the
	    #   computation of a character sequence.  It can be used by font
	    #   designers to indicate some characteristics of the glyph.  The
	    #   suffix may contain periods or any other permitted characters.
	    #   Small cap A, for example, could be named `uni0041.sc' or
	    #   `A.sc'.

	    next if $ch =~ /\./;

	    # Step 2:
	    #  Split the remaining string into a sequence of components,
	    #  using the underscore character (U+005F LOW LINE) as the
	    #  delimiter.

	    while ($ch =~ /([^_]+)/g) {
		$component = $1;

		# Step 3:
		#   Map each component to a character string according to the
		#   procedure below:
		#
		#   * If the component is in the Adobe Glyph List, then map
		#     it to the corresponding character in that list.

		$utmp = $AGL_to_unicode{$component};
		if ($utmp) {
		    $utmp = "U+" . $utmp;
		}

		#   * Otherwise, if the component is of the form `uni'
		#     (U+0075 U+006E U+0069) followed by a sequence of
		#     uppercase hexadecimal digits (0 .. 9, A .. F, i.e.,
		#     U+0030 .. U+0039, U+0041 .. U+0046), the length of
		#     that sequence is a multiple of four, and each group of
		#     four digits represents a number in the set {0x0000 ..
		#     0xD7FF, 0xE000 .. 0xFFFF}, then interpret each such
		#     number as a Unicode scalar value and map the component
		#     to the string made of those scalar values.

		elsif ($component =~ /^uni([0-9A-F]{4})+$/) {
		    while ($component =~ /([0-9A-F]{4})/g) {
			$nv = hex("0x" . $1);
			if ($nv <= 0xD7FF || $nv >= 0xE000) {
			    $utmp .= "U+" . $1;
			}
			else {
			    $utmp = "";
			    last;
			}
		    }
		}

		#   * Otherwise, if the component is of the form `u' (U+0075)
		#     followed by a sequence of four to six uppercase
		#     hexadecimal digits {0 .. 9, A .. F} (U+0030 .. U+0039,
		#     U+0041 .. U+0046), and those digits represent a number
		#     in {0x0000 .. 0xD7FF, 0xE000 .. 0x10FFFF}, then
		#     interpret this number as a Unicode scalar value and map
		#     the component to the string made of this scalar value.

		elsif ($component =~ /^u([0-9A-F]{4,6})$/) {
		    $nv = hex("0x" . $1);
		    if ($nv <= 0xD7FF || ($nv >= 0xE000 && $nv <= 0x10FFFF)) {
			$utmp = "U+" . $1;
		    }
		}

		# Finally, concatenate those strings; the result is the
		# character string to which the glyph name is mapped.

		$ucomp .= $utmp if $utmp;
	    }

	    # Unicode decomposition
	    while ($ucomp =~ /([0-9A-F]{4,6})/g) {
		$component = $1;
		$utmp = $unicode_decomposed{$component};
		$u .= "_" . ($utmp ? $utmp : $component);
	    }
	    $u =~ s/^_/u/;
	    if ($u) {
		if (defined $mapped{$u}) {
		    warn "both $mapped{$u} and $ch map to $u";
		}
		else {
		    $mapped{$u} = $ch;
		}
		$nmap{$ch} += 1;
		$map{$ch, "0"} = $u;
	    }
	}
    }
}

# Check explicitly for groff's standard ligatures -- many afm files don't
# have proper `L' entries.

my %default_ligatures = (
  "fi", "f i",
  "fl", "f l",
  "ff", "f f",
  "ffi", "ff i",
  "ffl", "ff l",
);

while (my ($lig, $components) = each %default_ligatures) {
    if (defined $width{$lig} && !defined $ligatures{$lig}) {
	$ligatures{$lig} = $components;
    }
}

# print it all out

open(FONT, ">$font") || die "$prog: can't open \`$font' for output: $!\n";
select(FONT);

print("# This file has been generated with " .
      "GNU afmtodit (groff) version @VERSION@\n");
print("#\n");
print("#   $fullname\n") if defined $fullname;
print("#   $version\n") if defined $version;
print("#   $familyname\n") if defined $familyname;

if ($opt_c) {
    print("#\n");
    if (defined $notice || @comments) {
	print("# The original AFM file contains the following comments:\n");
	print("#\n");
	print("#   $notice\n") if defined $notice;
	foreach my $comment (@comments) {
	    print("#   $comment\n");
	}
    }
    else {
	print("# The original AFM file contains no comments.\n");
    }
}

print("\n");

print("name $font\n");
print("internalname $psname\n") if $psname;
print("special\n") if $opt_s;
printf("slant %g\n", $italic_angle) if $italic_angle != 0;
printf("spacewidth %d\n", conv($width{"space"})) if defined $width{"space"};

if ($opt_e) {
    my $e = $opt_e;
    $e =~ s@.*/@@;
    print("encoding $e\n");
}

if (!$opt_n && %ligatures) {
    print("ligatures");
    while (my $lig = each %ligatures) {
	print(" $lig");
    }
    print(" 0\n");
}

if (!$opt_k && $#kern1 >= 0) {
    print("\n");
    print("kernpairs\n");

    for (my $i = 0; $i <= $#kern1; $i++) {
	my $c1 = $kern1[$i];
	my $c2 = $kern2[$i];
	if (defined $nmap{$c1} && $nmap{$c1} != 0
	    && defined $nmap{$c2} && $nmap{$c2} != 0) {
	    for (my $j = 0; $j < $nmap{$c1}; $j++) {
		for (my $k = 0; $k < $nmap{$c2}; $k++) {
		    if ($kernx[$i] != 0) {
			printf("%s %s %d\n",
			       $map{$c1, $j},
			       $map{$c2, $k},
			       conv($kernx[$i]));
		    }
		}
	    }
	}
    }
}

my ($asc_boundary, $desc_boundary, $xheight, $slant);

# characters not shorter than asc_boundary are considered to have ascenders

$asc_boundary = 0;
$asc_boundary = $height{"t"} if defined $height{"t"};
$asc_boundary -= 1;

# likewise for descenders

$desc_boundary = 0;
$desc_boundary = $depth{"g"} if defined $depth{"g"};
$desc_boundary = $depth{"j"} if defined $depth{"g"} && $depth{"j"} < $desc_boundary;
$desc_boundary = $depth{"p"} if defined $depth{"p"} && $depth{"p"} < $desc_boundary;
$desc_boundary = $depth{"q"} if defined $depth{"q"} && $depth{"q"} < $desc_boundary;
$desc_boundary = $depth{"y"} if defined $depth{"y"} && $depth{"y"} < $desc_boundary;
$desc_boundary -= 1;

if (defined $height{"x"}) {
    $xheight = $height{"x"};
}
elsif (defined $height{"alpha"}) {
    $xheight = $height{"alpha"};
}
else {
    $xheight = 450;
}

$italic_angle = $italic_angle*3.14159265358979323846/180.0;
$slant = sin($italic_angle)/cos($italic_angle);
$slant = 0 if $slant < 0;

print("\n");
print("charset\n");
for (my $i = 0; $i <= $#encoding; $i++) {
    my $ch = $encoding[$i];
    if (defined $ch && $ch ne "" && $ch ne "space") {
	$map{$ch, "0"} = "---" if !defined $nmap{$ch} || $nmap{$ch} == 0;
	my $type = 0;
	my $h = $height{$ch};
	$h = 0 if $h < 0;
	my $d = $depth{$ch};
	$d = 0 if $d < 0;
	$type = 1 if $d >= $desc_boundary;
	$type += 2 if $h >= $asc_boundary;
	printf("%s\t%d", $map{$ch, "0"}, conv($width{$ch}));
	my $italic_correction = 0;
	my $left_math_fit = 0;
	my $subscript_correction = 0;
	if (defined $opt_i) {
	    $italic_correction = $right_side_bearing{$ch} + $opt_i;
	    $italic_correction = 0 if $italic_correction < 0;
	    $subscript_correction = $slant * $xheight * .8;
	    $subscript_correction = $italic_correction if
		$subscript_correction > $italic_correction;
	    $left_math_fit = $left_side_bearing{$ch} + $opt_i;
	    if (defined $opt_m) {
		$left_math_fit = 0 if $left_math_fit < 0;
	    }
	}
	if (defined $italic_correction{$ch}) {
	    $italic_correction = $italic_correction{$ch};
	}
	if (defined $left_italic_correction{$ch}) {
	    $left_math_fit = $left_italic_correction{$ch};
	}
	if (defined $subscript_correction{$ch}) {
	    $subscript_correction = $subscript_correction{$ch};
	}
	if ($subscript_correction != 0) {
	    printf(",%d,%d", conv($h), conv($d));
	    printf(",%d,%d,%d", conv($italic_correction),
		   conv($left_math_fit),
		   conv($subscript_correction));
	}
	elsif ($left_math_fit != 0) {
	    printf(",%d,%d", conv($h), conv($d));
	    printf(",%d,%d", conv($italic_correction),
		   conv($left_math_fit));
	}
	elsif ($italic_correction != 0) {
	    printf(",%d,%d", conv($h), conv($d));
	    printf(",%d", conv($italic_correction));
	}
	elsif ($d != 0) {
	    printf(",%d,%d", conv($h), conv($d));
	}
	else {
	    # always put the height in to stop groff guessing
	    printf(",%d", conv($h));
	}
	printf("\t%d", $type);
	printf("\t%d\t%s\n", $i, $ch);
	if (defined $nmap{$ch}) {
	    for (my $j = 1; $j < $nmap{$ch}; $j++) {
		printf("%s\t\"\n", $map{$ch, $j});
	    }
	}
    }
    if (defined $ch && $ch eq "space" && defined $width{"space"}) {
	printf("space\t%d\t0\t%d\tspace\n", conv($width{"space"}), $i);
    }
}

sub conv {
    $_[0]*$unitwidth*$resolution/(72*1000*$sizescale) + ($_[0] < 0 ? -.5 : .5);
}

# eof
