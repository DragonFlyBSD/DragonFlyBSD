<?xml version="1.0" encoding="iso-8859-1"?>
<!-- $DragonFly: doc/share/sgml/mirrors-master.xsl,v 1.2 2004/04/08 18:25:51 justin Exp $ -->

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">

  <xsl:output type="xml" encoding="iso-8859-1"
	      omit-xml-declaration="yes"
	      indent="yes"/>

  <!-- these params should be externally bound. The values
       here are not used actually -->
  <xsl:param name="type" select="''" />
  <xsl:param name="proto" select="''" />
  <xsl:param name="target" select="''" />

  <xsl:param name="mirrors-docbook-country-anchor-id" select="translate($target, '/.', '--')" />

  <xsl:variable name="date">
    <xsl:value-of xmlns:cvs="http://www.FreeBSD.org/XML/CVS"
                  select="normalize-space(//cvs:keyword[@name='freebsd'])"/>
  </xsl:variable>

  <!--
     templates available:

        * "mirrors-lastmodified"
        * "mirrors-docbook-contact"
        * "mirrors-docbook-country-index-all"
        * "mirrors-docbook-variablelist"
        * "mirrors-docbook-itemizedlist"
  -->

  <xsl:template match="/">
    <xsl:choose>
      <xsl:when test="$target = 'handbook/mirrors/chapter.sgml'">
	<xsl:call-template name="mirrors-docbook-country-index-all" />
	<para>(<xsl:call-template name="mirrors-lastmodified" />)</para>
	<xsl:call-template name="mirrors-docbook-variablelist" />
      </xsl:when>
      <xsl:when test="$target = 'handbook/eresources/chapter.sgml'">
	<xsl:call-template name="mirrors-docbook-country-index-all" />
	<para>(<xsl:call-template name="mirrors-lastmodified" />)</para>
	<xsl:call-template name="mirrors-docbook-itemizedlist" />
      </xsl:when>
      <xsl:otherwise>
	<xsl:value-of select="'*** processing error ***'" />
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <!-- template: "mirrors-docbook-contact" -->

  <xsl:template name="mirrors-docbook-contact">
    <xsl:param name="email" select="'someone@somewhere'"/>

    <para>In case of problems, please contact the hostmaster
      <email><xsl:value-of select="$email" /></email> for this domain.</para>
  </xsl:template>

  <!-- template: "mirrors-lastmodified" -->

  <xsl:template name="mirrors-lastmodified">
    <xsl:text>as of </xsl:text>
    <xsl:call-template name="mirrors-lastmodified-utc" />
  </xsl:template>

  <!-- template: "mirrors-docbook-country-index-all" -->

  <xsl:template name="mirrors-docbook-country-index-all">
    <para>
      <xsl:for-each select="mirrors/entry[country/@role = 'primary'
	                    and host[@type = $type]]">
	<xsl:call-template name="mirrors-docbook-country-index">
	  <xsl:with-param name="mirrors-docbook-country-index-without-period" select="'true'" />
	</xsl:call-template>
      </xsl:for-each>

      <xsl:for-each select="mirrors/entry[(not(country/@role) or country/@role != 'primary') and
	                    host[@type = $type]]">
	<xsl:sort select="country/@sortkey" data-type="number"/>
	<xsl:sort select="country" />

	<xsl:call-template name="mirrors-docbook-country-index">
	  <xsl:with-param name="mirrors-docbook-country-index-without-period" select="'false'" />
	</xsl:call-template>
      </xsl:for-each>
    </para>
  </xsl:template>

  <xsl:template name="mirrors-docbook-country-index">
    <link>
      <xsl:attribute name="linkend">
	<xsl:value-of select="concat($mirrors-docbook-country-anchor-id, '-', @id, '-', $type)" />
      </xsl:attribute>
      <xsl:value-of select="country" />
    </link>
    <xsl:choose>
      <xsl:when test='$mirrors-docbook-country-index-without-period != "true" and
	position() = last()'><xsl:text>.</xsl:text></xsl:when>
      <xsl:otherwise><xsl:text>, </xsl:text></xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <!-- template: "mirrors-docbook-variablelist" -->

  <xsl:template name="mirrors-docbook-variablelist">
    <variablelist>
      <xsl:for-each select="mirrors/entry[country/@role = 'primary' and
	                    host[@type = $type]]">
	<xsl:call-template name="mirrors-docbook-variablelist-entry" />
      </xsl:for-each>

      <xsl:for-each select="mirrors/entry[(not(country/@role) or country/@role != 'primary') and
	                    host[@type = $type]]">
	<xsl:sort select="country/@sortkey" data-type="number"/>
	<xsl:sort select="country" />

	<xsl:call-template name="mirrors-docbook-variablelist-entry" />
      </xsl:for-each>
    </variablelist>
  </xsl:template>

  <xsl:template name="mirrors-docbook-variablelist-entry">
    <varlistentry>
      <term>
	<anchor>
	  <xsl:attribute name="id">
	    <xsl:value-of select="concat($mirrors-docbook-country-anchor-id, '-', @id, '-', $type)" />
	  </xsl:attribute>
	</anchor>
	<xsl:value-of select="country" />
      </term>

      <listitem>
	<xsl:if test="$type = 'ftp' and email">
	  <xsl:call-template name="mirrors-docbook-contact">
	    <xsl:with-param name="email" select="email" />
	  </xsl:call-template>
	</xsl:if>

	<itemizedlist>
	  <xsl:for-each select="host[@type = $type]">
	    <listitem>
	      <para>
		<xsl:choose>
		  <xsl:when test="url[@proto = $proto]">
		    <xsl:for-each select="url[@proto = $proto]">
		      <ulink>
			<xsl:attribute name="url"><xsl:value-of select="." /></xsl:attribute>
			<xsl:value-of select="name" />
		      </ulink>
		    </xsl:for-each>

		    <xsl:value-of select="' (ftp'" />

		    <xsl:choose>
		      <xsl:when test="url[@proto != $proto]">
			<xsl:for-each select="url[@proto != $proto]">
			  <xsl:value-of select="' / '" />
			  <xsl:choose>
			    <xsl:when test=". != ''">
			      <ulink>
				<xsl:attribute name="url"><xsl:value-of select="." /></xsl:attribute>
				<xsl:value-of select="@proto" />
			      </ulink>
			    </xsl:when>
			    <xsl:otherwise>
			      <xsl:value-of select="@proto" />
			    </xsl:otherwise>
			  </xsl:choose>
			</xsl:for-each>
		      </xsl:when>
		    </xsl:choose>

		    <xsl:value-of select="') '" />
		  </xsl:when>

		  <xsl:otherwise>
		    <xsl:value-of select="name" />
		  </xsl:otherwise>
		</xsl:choose>
	      </para>
	    </listitem>
	  </xsl:for-each>
	</itemizedlist>
      </listitem>
    </varlistentry>
  </xsl:template>

  <!-- template: "mirrors-docbook-itemizedlist" -->

  <xsl:template name="mirrors-docbook-itemizedlist">
    <itemizedlist>
      <xsl:for-each select="mirrors/entry[country/@role = 'primary' and
	                    host[@type = $type]]">
	<xsl:call-template name="mirrors-docbook-itemizedlist-listitem" />
      </xsl:for-each>

      <xsl:for-each select="mirrors/entry[(not(country/@role) or country/@role != 'primary') and
	                    host[@type = $type]]">
	<xsl:sort select="country/@sortkey" data-type="number"/>
	<xsl:sort select="country" />

	<xsl:call-template name="mirrors-docbook-itemizedlist-listitem" />
      </xsl:for-each>
    </itemizedlist>
  </xsl:template>

  <xsl:template name="mirrors-docbook-itemizedlist-listitem">
    <listitem>
      <anchor>
	<xsl:attribute name="id">
	  <xsl:value-of select="concat($mirrors-docbook-country-anchor-id, '-', @id, '-', $type)" />
	</xsl:attribute>
      </anchor>

      <para><xsl:value-of select="country" /></para>

      <itemizedlist>
	<xsl:for-each select="host[@type = $type]">
	  <listitem>
	    <para>
	      <xsl:choose>
		<xsl:when test="url[@proto = $proto]">
		  <xsl:for-each select="url[@proto = $proto]">
		    <ulink>
		      <xsl:attribute name="url"><xsl:value-of select="." /></xsl:attribute>
		      <xsl:value-of select="name" />
		    </ulink>
		  </xsl:for-each>

		  <xsl:if test="url[
		    contains(@proto, 'ftpv6')
		    or contains(@proto, 'httpv6')
		    or contains(@proto, 'rsyncv6')]">
		    <xsl:text> (IPv6)</xsl:text>
		  </xsl:if>
		</xsl:when>

		<xsl:otherwise>
		  <xsl:value-of select="name" />
		</xsl:otherwise>
	      </xsl:choose>
	    </para>
	  </listitem>
	</xsl:for-each>
      </itemizedlist>
    </listitem>
  </xsl:template>

  <!-- template: "mirrors-lastmodified-utc" -->

  <xsl:template name="mirrors-lastmodified-utc">
    <xsl:param name="basestr" select="substring-after(substring-after($date, ',v '), ' ')" />
    <xsl:param name="datestr" select="substring-before($basestr, ' ')" />
    <xsl:param name="timestr" select="substring-before(substring-after($basestr, ' '), ' ')" />

    <xsl:value-of select="concat($datestr, ' ', $timestr, ' UTC')" />
  </xsl:template>
</xsl:stylesheet>
