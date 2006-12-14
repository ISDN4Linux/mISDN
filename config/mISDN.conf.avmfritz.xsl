<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" version="1.0" encoding="UTF-8" indent="yes"/>

<!--
	Card Type: avmfritz
	Ports: 1
	Port Attributes: mode=(te|nt), link=(ptp|ptmp), capi=(yes|no)
-->
<xsl:template name="avmfritzcard">
<xsl:text>
</xsl:text>
</xsl:template>

<xsl:template name="avmfritzport">
<xsl:text> layermask:</xsl:text>
<xsl:choose>
<xsl:when test="@mode='nt'">
 <xsl:text>3</xsl:text>
</xsl:when>
<xsl:when test="@capi='yes'">
 <xsl:text>0</xsl:text>
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

<xsl:text> capi:</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@capi" />
 <xsl:with-param name="val-true">yes</xsl:with-param>
 <xsl:with-param name="val-false">no</xsl:with-param>
 <xsl:with-param name="val-default">no</xsl:with-param>
</xsl:call-template>

<xsl:text>
</xsl:text>
</xsl:template>

</xsl:stylesheet>
