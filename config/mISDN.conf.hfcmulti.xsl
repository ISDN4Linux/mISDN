<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" version="1.0" encoding="UTF-8" indent="yes"/>

<!--
	Module: hfcmulti
	Options: poll=<number>, pcm=<number>, debug=<number>, timer=(yes|no)
-->
<xsl:template name="HFCMULTImodule">

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> poll=</xsl:with-param>
 <xsl:with-param name="val" select="@poll" />
 <xsl:with-param name="val-default">128</xsl:with-param>
</xsl:call-template>

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> pcm=</xsl:with-param>
 <xsl:with-param name="val" select="@pcm" />
</xsl:call-template>

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> debug=</xsl:with-param>
 <xsl:with-param name="val" select="@debug" />
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>

<xsl:call-template name="if-set-match">
 <xsl:with-param name="prefix"> timer=</xsl:with-param>
 <xsl:with-param name="val" select="@timer" />
 <xsl:with-param name="val-default">0</xsl:with-param>
 <xsl:with-param name="val-true">1</xsl:with-param>
</xsl:call-template>

<xsl:text>
</xsl:text>

</xsl:template>

</xsl:stylesheet>
