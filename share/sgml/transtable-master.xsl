<?xml version="1.0" encoding="iso-8859-1"?>
<!-- $DragonFly: doc/share/sgml/transtable-master.xsl,v 1.1.1.1 2004/04/02 09:37:16 hmp Exp $
<!-- $DragonFly: doc/share/sgml/transtable-master.xsl,v 1.1.1.1 2004/04/02 09:37:16 hmp Exp $

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">

  <xsl:import href="./transtable-common.xsl" />

  <xsl:output type="xml" encoding="iso-8859-1"
	      indent="yes"/>

  <!-- these params should be externally bound. The values
       here are not used actually -->
  <xsl:param name="transtable-target-element" select="''" />
  <xsl:param name="transtable-word-group" select="''" />
  <xsl:param name="transtable-mode" select="''" />

  <xsl:template match="/">
    <xsl:choose>
      <xsl:when test="$transtable-mode = 'sortkey'">
	<xsl:apply-templates mode="sortkey"/>
      </xsl:when>
      <xsl:otherwise>
	<xsl:apply-templates />
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="*">
    <xsl:copy>
      <xsl:copy-of select="@*" />
      <xsl:apply-templates />
    </xsl:copy>
  </xsl:template>

  <xsl:template match="*[local-name() = $transtable-target-element]">
    <xsl:element name="{local-name(.)}" namespace="{namespace-uri(.)}">
      <xsl:copy-of select="@*" />

      <xsl:call-template name="transtable-sortkey-lookup">
	<xsl:with-param name="word" select="." />
	<xsl:with-param name="word-group" select="$transtable-word-group" />
      </xsl:call-template>

      <xsl:call-template name="transtable-lookup">
        <xsl:with-param name="word" select="." />
        <xsl:with-param name="word-group" select="$transtable-word-group" />
      </xsl:call-template>
    </xsl:element>
  </xsl:template>

 <!-- mode for generating sortkeytable -->

  <xsl:template match="*" mode="sortkey">
    <xsl:apply-templates mode="sortkey"/>
  </xsl:template>

  <xsl:template match="text()" mode="sortkey">
  </xsl:template>

  <xsl:template match="*[local-name() = $transtable-target-element]" mode="sortkey">
    <xsl:text>  </xsl:text>

    <xsl:comment>
      <xsl:call-template name="transtable-lookup">
	<xsl:with-param name="word" select="." />
	<xsl:with-param name="word-group" select="$transtable-word-group" />
      </xsl:call-template>
    </xsl:comment>

    <xsl:element name="word">
      <xsl:attribute name="name">
	<xsl:call-template name="transtable-lookup">
	  <xsl:with-param name="word" select="." />
	<xsl:with-param name="word-group" select="$transtable-word-group" />
	</xsl:call-template>
      </xsl:attribute>

      <xsl:attribute name="orig">
	<xsl:value-of select="." />
      </xsl:attribute>

      <xsl:attribute name="sortkey">
	<xsl:value-of select="'@sortkey@'" />
      </xsl:attribute>
    </xsl:element>

    <xsl:text disable-output-escaping="yes">&#10;</xsl:text>

  </xsl:template>
</xsl:stylesheet>
