<?xml version="1.0" encoding="iso-8859-1"?>
<!-- $DragonFly: doc/share/sgml/transtable-common.xsl,v 1.1.1.1 2004/04/02 09:37:15 hmp Exp $

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">

  <!-- these params should be externally bound. The values
       here are not used actually -->
  <xsl:param name="transtable.xml" select="'./transtable.xml'" />
  <xsl:param name="transtable-sortkey.xml" select="'./transtable-sortkey.xml'" />

  <xsl:key name="transtable-lookup-key" match="word" use="orig" />
  <xsl:key name="transtable-sortkey-lookup-key" match="word" use="@orig" />

  <xsl:template name="transtable-lookup">
    <xsl:param name="word" select="''"/>
    <xsl:param name="word-group" select="''"/>

    <xsl:choose>
      <xsl:when test="document($transtable.xml)/transtable/group[@id = $word-group]">
	<xsl:for-each select="document($transtable.xml)/transtable/group[@id = $word-group]">
	  <xsl:choose>
	    <xsl:when test="key('transtable-lookup-key', $word)">
	      <xsl:for-each select="key('transtable-lookup-key', $word)">
		<xsl:value-of select="tran" />
	      </xsl:for-each>
	    </xsl:when>
	    <xsl:otherwise>
	      <xsl:value-of select="$word" />
	    </xsl:otherwise>
	  </xsl:choose>
	</xsl:for-each>
      </xsl:when>
      <xsl:otherwise>
	<xsl:value-of select="$word" />
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template name="transtable-sortkey-lookup">
    <xsl:param name="word" select="''"/>

    <xsl:for-each select="document($transtable-sortkey.xml)/sortkeys">
      <xsl:for-each select="key('transtable-sortkey-lookup-key', $word)">
	<xsl:attribute name="sortkey">
	  <xsl:value-of select="@sortkey" />
	</xsl:attribute>
      </xsl:for-each>
    </xsl:for-each>
  </xsl:template>
</xsl:stylesheet>
