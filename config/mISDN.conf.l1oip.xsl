<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" version="1.0" encoding="UTF-8" indent="yes"/>

<!--
	Module: l1oip
	Options: poll=<number>, pcm=<number>, debug=<number>, timer=(yes|no)
-->
<xsl:template name="L1OIPmodule">

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> debug=</xsl:with-param>
 <xsl:with-param name="val" select="@debug" />
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>

<xsl:text>
</xsl:text>

</xsl:template>

<!--
	Card Type: l1oip
	Ports: n
	Port Attributes: mode=(te|nt), link=(ptp|ptmp), type=(bri|pri|bri-mc|pri-mc), codec=0, ip=n,n,n,n port=<number>,
                     localport=<number>, ondemand=(0|1), id=<random-id>
-->
<xsl:template name="L1OIPcard">
<xsl:text>
</xsl:text>
</xsl:template>

<xsl:template name="L1OIPport">
<xsl:text> type:</xsl:text>
<xsl:choose>
<xsl:when test="@type='bri'">
 <xsl:text>1</xsl:text>
</xsl:when>
<xsl:when test="@type='pri'">
 <xsl:text>2</xsl:text>
</xsl:when>
<xsl:when test="@type='bri-mc'">
 <xsl:text>3</xsl:text>
</xsl:when>
<xsl:when test="@type='pri-mc'">
 <xsl:text>4</xsl:text>
</xsl:when>
<xsl:otherwise>
 <xsl:text>1</xsl:text>
</xsl:otherwise>
</xsl:choose>

<xsl:text> layermask:</xsl:text>
<xsl:choose>
<xsl:when test="@mode='nt'">
 <xsl:text>3</xsl:text>
</xsl:when>
<xsl:otherwise>
 <xsl:text>15</xsl:text>
</xsl:otherwise>
</xsl:choose>

<xsl:text> protocol:</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@mode" />
 <xsl:with-param name="match-true">te</xsl:with-param>
 <xsl:with-param name="match-false">nt</xsl:with-param>
 <xsl:with-param name="val-true">34</xsl:with-param>
 <xsl:with-param name="val-false">18</xsl:with-param>
 <xsl:with-param name="val-default">34</xsl:with-param>
</xsl:call-template>
<xsl:if test="@mode!='nt'">
 <xsl:text>+</xsl:text>
 <xsl:call-template name="if-match">
  <xsl:with-param name="val" select="@link" />
  <xsl:with-param name="match-true">ptp</xsl:with-param>
  <xsl:with-param name="match-false">ptmp</xsl:with-param>
  <xsl:with-param name="val-true">0</xsl:with-param>
  <xsl:with-param name="val-false">(-32)</xsl:with-param>
  <xsl:with-param name="val-default">(-32)</xsl:with-param>
 </xsl:call-template>
</xsl:if>

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> codec:</xsl:with-param>
 <xsl:with-param name="val" select="@codec" />
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> ip:</xsl:with-param>
 <xsl:with-param name="val" select="@ip" />
 <xsl:with-param name="val-default">0,0,0,0</xsl:with-param>
</xsl:call-template>

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> port:</xsl:with-param>
 <xsl:with-param name="val" select="@port" />
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> localport:</xsl:with-param>
 <xsl:with-param name="val" select="@localport" />
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> ondemand:</xsl:with-param>
 <xsl:with-param name="val" select="@ondemand" />
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> id:</xsl:with-param>
 <xsl:with-param name="val" select="@id" />
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>

<xsl:text>
</xsl:text>
</xsl:template>

</xsl:stylesheet>
