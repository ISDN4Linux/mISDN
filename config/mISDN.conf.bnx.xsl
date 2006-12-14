<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" version="1.0" encoding="UTF-8" indent="yes"/>

<!--
	Card Type: BN2S0, BN4S0, BN8S0
	Ports: 2, 4, 8
	Card Attributes: ulaw=(yes|no), dtmf=(yes|no), pcm_slave=(yes|no), ignore_pcm_frameclock=(yes|no),
	                 rxclock=(yes|no), crystalclock=(yes|no), watchdog=(yes|no)
	Port Attributes: mode=(te|nt), link=(ptp|ptmp), master-clock=(yes|no), capi=(yes|no)
-->
<xsl:template name="type-options">
<xsl:param name="force-pcm-slave">no</xsl:param>

<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@ulaw" />
 <xsl:with-param name="val-true">(2**8)</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@dtmf" />
 <xsl:with-param name="val-true">(2**9)</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:choose>
 <xsl:when test="$force-pcm-slave='yes'">
  <xsl:text>(2**11)</xsl:text>
 </xsl:when>
 <xsl:otherwise>
  <xsl:call-template name="if-match">
   <xsl:with-param name="val" select="@pcm_slave" />
   <xsl:with-param name="val-true">(2**11)</xsl:with-param>
  </xsl:call-template>
 </xsl:otherwise>
</xsl:choose>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@ignore_pcm_frameclock" />
 <xsl:with-param name="val-true">(2**12)</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@rxclock" />
 <xsl:with-param name="val-true">(2**13)</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@crystalclock" />
 <xsl:with-param name="val-true">(2**18)</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@watchdog" />
 <xsl:with-param name="val-true">(2**19)</xsl:with-param>
</xsl:call-template>
</xsl:template>

<xsl:template name="BN2S0card">
<xsl:param name="type">4</xsl:param>
<xsl:value-of select="concat(' type:',$type,'+')" />
<xsl:call-template name="type-options" />
<xsl:text>
</xsl:text>
</xsl:template>

<xsl:template name="BN2S0port">

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
<xsl:text>+</xsl:text>
<xsl:if test="@mode!='nt'">
 <xsl:call-template name="if-match">
  <xsl:with-param name="val" select="@link" />
  <xsl:with-param name="match-true">ptp</xsl:with-param>
  <xsl:with-param name="match-false">ptmp</xsl:with-param>
  <xsl:with-param name="val-true">0</xsl:with-param>
  <xsl:with-param name="val-false">(-32)</xsl:with-param>
  <xsl:with-param name="val-default">(-32)</xsl:with-param>
 </xsl:call-template>
 <xsl:text>+</xsl:text>
</xsl:if>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@master-clock" />
 <xsl:with-param name="val-true">(2**16)</xsl:with-param>
</xsl:call-template>

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

<xsl:template name="BN4S0card">
<xsl:call-template name="BN2S0card">
 <xsl:with-param name="type">4</xsl:with-param>
</xsl:call-template>
</xsl:template>

<xsl:template name="BN4S0port">
<xsl:call-template name="BN2S0port" />
</xsl:template>

<xsl:template name="BN8S0card">
<xsl:call-template name="BN2S0card">
 <xsl:with-param name="type">8</xsl:with-param>
</xsl:call-template>
</xsl:template>

<xsl:template name="BN8S0port">
<xsl:call-template name="BN2S0port" />
</xsl:template>

<!--
	Card Type: BN2E1
	Ports: 2
	Card Attributes: ulaw=(yes|no), dtmf=(yes|no), pcm_slave=(yes|no), ignore_pcm_frameclock=(yes|no),
	                 rxclock=(yes|no), crystalclock=(yes|no), watchdog=(yes|no)
	Port Attributes: mode=(te|nt), link=(ptp|ptmp), optical=(yes|no), los=(yes|no), ais=(yes|no),
	                 slip=(yes|no), nocrc4=(yes|no), capi=(yes|no)
-->
<xsl:template name="BN2E1card">
<xsl:text> type:1+</xsl:text>
<xsl:call-template name="type-options" />
<xsl:text>,1+</xsl:text>
<xsl:call-template name="type-options">
 <xsl:with-param name="force-pcm-slave">yes</xsl:with-param>
</xsl:call-template>
<xsl:text>
</xsl:text>
</xsl:template>

<xsl:template name="BN2E1port">
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
<xsl:text>+</xsl:text>
<xsl:if test="@mode!='nt'">
 <xsl:call-template name="if-match">
  <xsl:with-param name="val" select="@link" />
  <xsl:with-param name="match-true">ptp</xsl:with-param>
  <xsl:with-param name="match-false">ptmp</xsl:with-param>
  <xsl:with-param name="val-true">0</xsl:with-param>
  <xsl:with-param name="val-false">(-32)</xsl:with-param>
  <xsl:with-param name="val-default">(-32)</xsl:with-param>
 </xsl:call-template>
 <xsl:text>+</xsl:text>
</xsl:if>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@optical" />
 <xsl:with-param name="val-true">(2**16)</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@los" />
 <xsl:with-param name="val-true">(2**18)</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@ais" />
 <xsl:with-param name="val-true">(2**19)</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@slip" />
 <xsl:with-param name="val-true">(2**21)</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@nocrc4" />
 <xsl:with-param name="val-true">(2**23)</xsl:with-param>
</xsl:call-template>

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
