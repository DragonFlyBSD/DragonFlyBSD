# 
# $DragonFly: doc/en/books/arch-handbook/Makefile,v 1.1.1.1 2004/04/02 09:35:47 hmp Exp $
#
# Build the FreeBSD Architecture Handbook.
#

MAINTAINER=doc@FreeBSD.org

DOC?= book

FORMATS?= html-split

#HAS_INDEX= true

INSTALL_COMPRESSED?= gz
INSTALL_ONLY_COMPRESSED?=

# 
# SRCS lists the individual SGML files that make up the document. Changes
# to any of these files will force a rebuild
#

# SGML content
SRCS=  book.sgml
SRCS+= boot/chapter.sgml
SRCS+= driverbasics/chapter.sgml
SRCS+= isa/chapter.sgml
SRCS+= jail/chapter.sgml
SRCS+= kobj/chapter.sgml
SRCS+= locking/chapter.sgml
SRCS+= newbus/chapter.sgml
SRCS+= pci/chapter.sgml
SRCS+= scsi/chapter.sgml
SRCS+= smp/chapter.sgml
SRCS+= sound/chapter.sgml
SRCS+= pccard/chapter.sgml
SRCS+= sysinit/chapter.sgml
SRCS+= usb/chapter.sgml
SRCS+= vm/chapter.sgml

# Images from the cross-document image library
IMAGES_LIB=  callouts/1.png
IMAGES_LIB+= callouts/2.png
IMAGES_LIB+= callouts/3.png
IMAGES_LIB+= callouts/4.png
IMAGES_LIB+= callouts/5.png
IMAGES_LIB+= callouts/6.png
IMAGES_LIB+= callouts/7.png
IMAGES_LIB+= callouts/8.png
IMAGES_LIB+= callouts/9.png
IMAGES_LIB+= callouts/10.png

# Entities

DOC_PREFIX?= ${.CURDIR}/../../..
.include "${DOC_PREFIX}/share/mk/doc.project.mk"
