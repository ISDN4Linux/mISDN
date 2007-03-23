<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" version="1.0" encoding="UTF-8" indent="yes"/>

<xsl:include href='mISDN.conf.inc.xsl' />
<xsl:include href='mISDN.conf.mISDN_dsp.xsl' />
<xsl:include href='mISDN.conf.hfcmulti.xsl' />
<xsl:include href='mISDN.conf.bnx.xsl' />
<xsl:include href='mISDN.conf.singlepci.xsl' />

<!--
	Main mISDNconf Template
-->
<xsl:template match="mISDNconf">

<!-- module -->

<xsl:for-each select="module">

<xsl:choose>

 <xsl:when test=".='hfcmulti'">
  <xsl:value-of select="concat('MODULE:',.)" />
  <xsl:call-template name="HFCMULTImodule" />
 </xsl:when>

 <xsl:when test=".='mISDN_dsp'">
  <xsl:value-of select="concat('MODULE:',.)" />
  <xsl:call-template name="MISDNDSPmodule" />
 </xsl:when>

 <xsl:otherwise>
  <xsl:value-of select="concat('MODULE:',.)" />
<xsl:text>
</xsl:text>
 </xsl:otherwise>

</xsl:choose>

</xsl:for-each>

<!-- devnode -->

<xsl:for-each select="devnode">

<xsl:choose>

 <xsl:when test=".='mISDN'">
  <xsl:value-of select="concat('DEVNODE:',.)" />
  <xsl:call-template name="if-set">
   <xsl:with-param name="prefix"> user:</xsl:with-param>
   <xsl:with-param name="val" select="@user" />
   <xsl:with-param name="val-default">root</xsl:with-param>
  </xsl:call-template>
  <xsl:call-template name="if-set">
   <xsl:with-param name="prefix"> group:</xsl:with-param>
   <xsl:with-param name="val" select="@group" />
   <xsl:with-param name="val-default">root</xsl:with-param>
  </xsl:call-template>
  <xsl:call-template name="if-set">
   <xsl:with-param name="prefix"> mode:</xsl:with-param>
   <xsl:with-param name="val" select="@mode" />
   <xsl:with-param name="val-default">644</xsl:with-param>
  </xsl:call-template>
 </xsl:when>
</xsl:choose>
<xsl:text>
</xsl:text>

</xsl:for-each>

<!-- card, port -->

<xsl:for-each select="card">

<xsl:choose>

 <xsl:when test="@type='BN2S0'">
  <xsl:value-of select="concat('CARD:',@type)" />
  <xsl:call-template name="BN2S0card" />
  <xsl:for-each select="port">
   <xsl:sort data-type="number" />
   <xsl:text>PORT:</xsl:text>
   <xsl:value-of select="." />
   <xsl:call-template name="BN2S0port" />
  </xsl:for-each>
 </xsl:when>

 <xsl:when test="@type='BN4S0'">
  <xsl:value-of select="concat('CARD:',@type)" />
  <xsl:call-template name="BN4S0card" />
  <xsl:for-each select="port">
   <xsl:sort data-type="number" />
   <xsl:text>PORT:</xsl:text>
   <xsl:value-of select="." />
   <xsl:call-template name="BN4S0port" />
  </xsl:for-each>
 </xsl:when>

 <xsl:when test="@type='BN8S0'">
  <xsl:value-of select="concat('CARD:',@type)" />
  <xsl:call-template name="BN8S0card" />
  <xsl:for-each select="port">
   <xsl:sort data-type="number" />
   <xsl:text>PORT:</xsl:text>
   <xsl:value-of select="." />
   <xsl:call-template name="BN8S0port" />
  </xsl:for-each>
 </xsl:when>

 <xsl:when test="@type='BN2E1'">
  <xsl:value-of select="concat('CARD:',@type)" />
  <xsl:call-template name="BN2E1card" />
  <xsl:for-each select="port">
   <xsl:sort data-type="number" />
   <xsl:text>PORT:</xsl:text>
   <xsl:value-of select="." />
   <xsl:call-template name="BN2E1port" />
  </xsl:for-each>
 </xsl:when>

 <xsl:when test="@type='hfcpci' or @type='avmfritz' or @type='w6692pci'">
  <xsl:value-of select="concat('CARD:',@type)" />
  <xsl:call-template name="singlepcicard" />
  <xsl:for-each select="port">
   <xsl:sort data-type="number" />
   <xsl:text>PORT:</xsl:text>
   <xsl:value-of select="." />
   <xsl:call-template name="singlepciport" />
  </xsl:for-each>
 </xsl:when>

</xsl:choose>

</xsl:for-each>
</xsl:template>
</xsl:stylesheet>
