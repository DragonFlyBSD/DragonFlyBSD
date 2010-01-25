# Copyright (C) 1989-2000, 2002, 2003, 2006, 2009
#   Free Software Foundation, Inc.
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

# This is set up so you can do
#   make -f generate/Makefile
# in the parent directory of this directory.

# The following line is required for DOS/Windows systems, since
# this Makefile needs a Unixy shell to run.
SHELL = /bin/sh

# Directory containing AFM files.  Must not be current directory.
# Either long names (eg Times-Roman.afm) or short names (eg timesr.afm)
# may be used. See the afmname script.
afmdir=/usr/local/lib/afm

# The symbol font which fits to `Symbol.afm'.  For the creation of
# `symbolsl.afm' we need the `printafm' script from ghostscript.
symbolfont=/usr/local/lib/fonts/Symbol.pfb

PRINTAFM=printafm

srcdir=generate

SPECIALFONTS=S
DINGBATSFONTS=ZD ZDR
GREEKFONTS=SS
TEXTFONTS=AB ABI AI AR BMB BMBI BMI BMR CB CBI CI CR HB HBI HI HR HNB HNBI \
  HNI HNR NB NBI NI NR PB PBI PI PR TB TBI TI TR ZCMI

FONTS=$(TEXTFONTS) $(SPECIALFONTS) $(GREEKFONTS) $(DINGBATSFONTS) EURO

DESC=$(srcdir)/../DESC
AFMTODIT=afmtodit -c -d$(DESC)
IFLAG=-i 50
RFLAG=-i 0 -m
NOLIGFLAG=-n
TEXTENC=$(srcdir)/../text.enc
EFLAG=-e $(TEXTENC)
TEXTMAP=$(srcdir)/textmap
AFMNAME=$(srcdir)/afmname

all: $(FONTS)

TR:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/Times-Roman.afm` $(TEXTMAP) $@

TB:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/Times-Bold.afm` $(TEXTMAP) $@

TI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) -a 7 \
	  `$(AFMNAME) $(afmdir)/Times-Italic.afm` $(TEXTMAP) $@

TBI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/Times-BoldItalic.afm` $(TEXTMAP) $@

HR:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/Helvetica.afm` $(TEXTMAP) $@

HB:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/Helvetica-Bold.afm` $(TEXTMAP) $@

HI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/Helvetica-Oblique.afm` $(TEXTMAP) $@

HBI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/Helvetica-BoldOblique.afm` $(TEXTMAP) $@

CR:
	$(AFMTODIT) $(NOLIGFLAG) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/Courier.afm` $(TEXTMAP) $@

CB:
	$(AFMTODIT) $(NOLIGFLAG) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/Courier-Bold.afm` $(TEXTMAP) $@

CI:
	$(AFMTODIT) $(NOLIGFLAG) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/Courier-Oblique.afm` $(TEXTMAP) $@

CBI:
	$(AFMTODIT) $(NOLIGFLAG) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/Courier-BoldOblique.afm` $(TEXTMAP) $@

PR:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/Palatino-Roman.afm` $(TEXTMAP) $@

PB:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/Palatino-Bold.afm` $(TEXTMAP) $@

PI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/Palatino-Italic.afm` $(TEXTMAP) $@

PBI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/Palatino-BoldItalic.afm` $(TEXTMAP) $@

NR:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/NewCenturySchlbk-Roman.afm` $(TEXTMAP) $@

NB:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/NewCenturySchlbk-Bold.afm` $(TEXTMAP) $@

NI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/NewCenturySchlbk-Italic.afm` $(TEXTMAP) $@

NBI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/NewCenturySchlbk-BoldItalic.afm` $(TEXTMAP) $@

BMR:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/Bookman-Light.afm` $(TEXTMAP) $@

BMB:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/Bookman-Demi.afm` $(TEXTMAP) $@

BMI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/Bookman-LightItalic.afm` $(TEXTMAP) $@

BMBI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/Bookman-DemiItalic.afm` $(TEXTMAP) $@

AR:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/AvantGarde-Book.afm` $(TEXTMAP) $@

AB:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/AvantGarde-Demi.afm` $(TEXTMAP) $@

AI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/AvantGarde-BookOblique.afm` $(TEXTMAP) $@

ABI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/AvantGarde-DemiOblique.afm` $(TEXTMAP) $@

HNR:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/Helvetica-Narrow.afm` $(TEXTMAP) $@

HNB:
	$(AFMTODIT) $(EFLAG) $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/Helvetica-Narrow-Bold.afm` $(TEXTMAP) $@

HNI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/Helvetica-Narrow-Oblique.afm` $(TEXTMAP) $@

HNBI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/Helvetica-Narrow-BoldOblique.afm` $(TEXTMAP) $@

ZCMI:
	$(AFMTODIT) $(EFLAG) $(IFLAG) \
	  `$(AFMNAME) $(afmdir)/ZapfChancery-MediumItalic.afm` $(TEXTMAP) $@

ZD:
	$(AFMTODIT) -s $(RFLAG) \
	  `$(AFMNAME) $(afmdir)/ZapfDingbats.afm` $(srcdir)/dingbats.map $@

SS: symbolsl.afm
	$(AFMTODIT) -s -x $(IFLAG) \
	  symbolsl.afm $(srcdir)/lgreekmap $@

S: symbol.afm
	$(AFMTODIT) -s $(RFLAG) \
	  symbol.afm symbolmap $@

ZDR: zapfdr.afm
	$(AFMTODIT) -s $(RFLAG) \
	  zapfdr.afm $(srcdir)/dingbats.rmap $@

# the map is just a dummy
EURO: freeeuro.afm
	$(AFMTODIT) $(RFLAG) \
	  freeeuro.afm symbolmap $@

freeeuro.afm freeeuro.pfa: $(srcdir)/freeeuro.sfd
	fontforge -script $(srcdir)/sfdtopfa.pe $(srcdir)/freeeuro.sfd

symbol.afm: $(srcdir)/symbol.sed
	-rm -f $@
	sed -f $(srcdir)/symbol.sed `$(AFMNAME) $(afmdir)/Symbol.afm` >$@

symbolsl.afm: $(srcdir)/symbolsl.awk $(srcdir)/../symbolsl.ps
	-rm -f $@
	-rm -f Fontmap
	echo "/Symbol-Slanted ($(srcdir)/../symbolsl.ps) ;"  > Fontmap
	echo "/Symbol         ($(symbolfont)) ;" >> Fontmap
	$(PRINTAFM) Symbol-Slanted > tmp.afm
	awk -f $(srcdir)/symbolsl.awk -v SYMAFM=`$(AFMNAME) \
	  $(afmdir)/Symbol.afm` tmp.afm >$@
	-rm -f Fontmap tmp.afm

zapfdr.afm: $(srcdir)/zapfdr.sed
	-rm -f $@
	sed -f $(srcdir)/zapfdr.sed \
	  `$(AFMNAME) $(afmdir)/ZapfDingbats.afm` >$@

ZD: $(srcdir)/dingbats.map
ZDR: $(srcdir)/dingbats.rmap
$(TEXTFONTS): $(TEXTMAP) $(TEXTENC)
$(SPECIALFONTS): symbolmap
$(GREEKFONTS): $(srcdir)/lgreekmap
$(FONTS): $(DESC)

symbolmap: $(TEXTMAP) $(srcdir)/symbolchars
	echo '#' >$@
	echo '# This is a list of all predefined groff symbols.' >>$@
	echo '#' >>$@
	cat $(TEXTMAP) $(srcdir)/symbolchars >>$@

clean:
	-rm -f symbolmap symbol.afm

realclean: clean
	-rm -f $(FONTS)

extraclean: realclean
	-rm -f core *~ "#*"

.PHONY: all clean realclean extraclean

