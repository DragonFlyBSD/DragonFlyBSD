<?xml version='1.0'?>

<!-- $DragonFly: doc/share/xsl/dragonfly.xsl,v 1.1.1.1 2004/04/02 09:37:20 hmp Exp $

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'
                xmlns="http://www.w3.org/TR/xhtml1/transitional"
                exclude-result-prefixes="#default">
  
  <!-- Pull in the base stylesheets -->
  <!-- XXX hardcoded path.  Very bad.  Should be turned in to a paramater -->
  <xsl:import href="/usr/local/share/xml/docbook/xsl/modular/html/docbook.xsl"/>
  
  <!-- Redefine variables, and replace templates as necessary here -->
  <xsl:template match="hostid|username|groupname|devicename|maketarget|makevar">
    <xsl:call-template name="inline.monoseq"/>
  </xsl:template>

</xsl:stylesheet>
