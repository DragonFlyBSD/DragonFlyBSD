<?xml version='1.0'?>

<!-- $DragonFly: doc/share/xsl/dragonfly-fo.xsl,v 1.1.1.1 2004/04/02 09:37:20 hmp Exp $

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'
                xmlns="http://www.w3.org/TR/xhtml1/transitional"
                exclude-result-prefixes="#default">

  <!-- Pull in the base stylesheets -->
  <xsl:import href="/usr/local/share/xsl/docbook/fo/docbook.xsl"/>

  <!-- Redefine variables, and replace templates as necessary here -->

  <xsl:param name="dragonfly.output.print"
             select="'0'"/>
  <xsl:param name="dragonfly.output.print.pdf"
             select="'0'"/>
  <xsl:param name="dragonfly.output.print.justify"
             select="'0'"/>
  <xsl:param name="dragonfly.output.print.twoside"
             select="'0'"/>

  <!-- Include the common stylesheets -->
  
  <xsl:include href="freebsd-common.xsl"/>

  <!-- FO specific customisation goes here -->

</xsl:stylesheet>

