<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="1.0"
	xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

<!-- bgcolor="#9ACD32" -->
<xsl:template match="/">
  <html>
  	<head>
  	  <title>DragonFlyBSD.ORG - Team Task List</title>
	  <meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
  	  <meta name="robots" content="all" />
  	  <meta name="author" content="Hiten Pandya" />
  	  <meta name="copyright" content="Copyright 2003, 2004" />
  	  <meta name="keywords"
  	  		content="DragonFly, BSD, DragonFlyBSD, FreeBSD, TODO, Tasks, UNIX,
  	  		Linux, Clustering, Single System Image, SSI, LWKT, Threading,
  	  		Threads, Next Generation POSIX Threads, NGPT" />
  	  
  	   <link rel="stylesheet" type="text/css" href="task-list.css" media="screen" />
  	</head>
  	
  	<body>
  		<h1><a href="http://www.dragonflybsd.org/">DragonFlyBSD</a> - Task List</h1>

  		<h2>Show Stopping (Very High Priority) Items</h2>
  		<xsl:for-each select="task-list/item[priority/@type = 'veryhigh']">
  		  <xsl:sort select="last-modified" order="descending" />
  		  <xsl:call-template name="item" />
  		</xsl:for-each>
  		<hr size="1" noshade="noshade" />
  		
  		<br />
  		<h2>Required (High Priority) Items</h2>
  		<xsl:for-each select="task-list/item[priority/@type = 'high']">
  		  <xsl:sort select="last-modified" order="descending" />
  		  <xsl:call-template name="item" />
  		</xsl:for-each>
  		
  		<hr size="1" noshade="noshade" />  		
  		<br />
  		
  		<h2>Desired (Medium Priority) Items</h2>
  		<xsl:for-each select="task-list/item[priority/@type = 'medium']">
  		  <xsl:sort select="last-modified" order="descending" />
  		  <xsl:call-template name="item" />
  		</xsl:for-each>
  		<hr size="1" noshade="noshade" />
  		
  		<h2>Additional (Low Priority) Items</h2>
  		<xsl:for-each select="task-list/item[priority/@type = 'low']">
  		  <xsl:sort select="last-modified" order="descending" />
  		  <xsl:call-template name="item" />
  		</xsl:for-each>
  		  		
  		<hr size="1" noshade="noshade" />
  		<p><code>Copyright (c) 2003, 2004
  		<a href="mailto:hmp[at]backplane[dot]com">Hiten Pandya</a>.
  		All rights reserved.</code></p>
  	</body>
  </html>
</xsl:template>

<xsl:template name="descpara" match="para">
    <xsl:value-of select="para" /><br />
</xsl:template>

<xsl:template name="item" match="item">
  <table cellspacing="0" cellpadding="6" border="1" width="640">
    <tr>
    	<!-- task long name -->
    	<th width="100" align="center">
    	  <xsl:variable name="idtag">
    	    <xsl:value-of select="./@id" />
    	  </xsl:variable>
    	  <a href="#{$idtag}"><xsl:value-of select="./@id" /></a>
    	</th>
    	
    	<th width="400" align="center">
    	  <xsl:value-of select="name" />
    	</th>
	    	
    	<!--
    	  task status: wip::done::blocked::suspended
    	-->
    	<xsl:variable name="ostatus">
    	  <xsl:value-of select="status/@type" />
    	</xsl:variable>
	    	
    	<th class="{$ostatus}" width="140" align="center">
    	  <xsl:choose>
    	    <xsl:when test="status/@type = 'work-in-progress'">
    	      Work in Progress
    	    </xsl:when>
    	    <xsl:when test="status/@type = 'done'">
    	      Done
    	    </xsl:when>
    	    <xsl:when test="status/@type = 'blocked'">
    	      Blocked
    	    </xsl:when>
    	    <xsl:when test="status/@type = 'suspended'">
    	      Suspended
    	    </xsl:when>  		    	    
    	    <xsl:otherwise>
    	      (incorrect status!)
    	    </xsl:otherwise>
    	  </xsl:choose>
    	</th>
    </tr>
	    
    <tr>
    	<!-- task description -->
    	<td colspan="2" width="500" align="left" valign="top">
    	  <xsl:for-each select="description/para">
    	    <p><xsl:value-of select="." /></p>
    	  </xsl:for-each>
    	</td>
	    	
    	<!-- task meta info. -->
    	<td width="140" align="right" valign="top">
    	  <table border="0" width="100%">
    	    <!-- <tr><th align="right">Responsible:</th></tr> -->
    	  <xsl:for-each select="owners/owner">
    	    <tr>
    	      <td align="right">
    	      <xsl:variable name="oemail">
    	        <xsl:value-of select="email" />
    	      </xsl:variable>
    	      <a href="mailto:{$oemail}"><xsl:value-of select="name" /></a>
    	      </td>
    	    </tr>
    	  </xsl:for-each>
    	    <tr><td></td></tr>
    	    <tr>
    	      <xsl:variable name="opri">
    	        <xsl:choose>
    	          <xsl:when test="priority/@type">
    	           <xsl:value-of select="priority/@type" />
    	          </xsl:when>
    	          <xsl:otherwise>
    	           low
    	          </xsl:otherwise>
    	        </xsl:choose>
    	      </xsl:variable>
    	      <td class="{$opri}" align="right" valign="bottom">
    	      <strong>
    	      <xsl:choose>
    	        <xsl:when test="priority/@type = 'veryhigh'">
    	         Very High Priority
    	        </xsl:when>
    	        <xsl:when test="priority/@type = 'high'">
    	         High Priority
    	        </xsl:when>
    	        <xsl:when test="priority/@type = 'medium'">
    	         Medium Priority
    	        </xsl:when>
    	        <xsl:when test="priority/@type = 'low'">
    	         Low Priority
    	        </xsl:when>
    	      </xsl:choose>
    	      </strong>
    	      </td>
    	    </tr>
    	  </table>
    	</td>
    </tr>
    
    <tr>
    	<td colspan="2" align="left">
    	  <strong>Dependancies:</strong>
    	  <code>
    	  <xsl:choose>
    	    <xsl:when test="dependancies/depend/@ident = 'NONE'">
    	      NONE
    	    </xsl:when>
    	    <xsl:otherwise>
    	      <xsl:for-each select="dependancies/depend">
    	        <xsl:variable name="atagident">
    	          <xsl:value-of select="./@ident" />
    	        </xsl:variable>
    	          <a href="#{$atagident}"><xsl:value-of select="./@ident" /></a>
    	      </xsl:for-each>
    	    </xsl:otherwise>
    	  </xsl:choose>
    	  </code>
    	</td>
	    	
    	<td align="left">
    	  <strong>Last Mod</strong>: <xsl:value-of select="last-modified" />
    	</td>  		    	
    </tr>
  </table>
  <br />
</xsl:template>

</xsl:stylesheet>
