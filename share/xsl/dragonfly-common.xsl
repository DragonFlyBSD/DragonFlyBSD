<?xml version='1.0'?>

<!-- $DragonFly: doc/share/xsl/dragonfly-common.xsl,v 1.1.1.1 2004/04/02 09:37:20 hmp Exp $

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'
                xmlns="http://www.w3.org/TR/xhtml1/transitional"
                exclude-result-prefixes="#default">
  
  <!-- Global customisation -->

  <!-- Redefine variables, and replace templates as necessary here -->
  <xsl:template match="hostid|username|groupname|devicename|maketarget|makevar">
    <xsl:call-template name="inline.monoseq"/>
  </xsl:template>

  <xsl:param name="toc.section.depth" select="1"/>
  <xsl:param name="section.autolabel" select="1"/>
  <xsl:param name="section.label.includes.component.label" select="1"/>

</xsl:stylesheet>
