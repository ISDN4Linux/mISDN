<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" version="1.0" encoding="UTF-8" indent="yes"/>

<!--
	Module: mISDN_dsp
	Options: debug=<number>, options=<number>, poll=<number>, dtmfthreshold=<number>
-->
<xsl:template name="MISDNDSPmodule">

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> debug=</xsl:with-param>
 <xsl:with-param name="val" select="@debug" />
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> options=</xsl:with-param>
 <xsl:with-param name="val" select="@options" />
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> poll=</xsl:with-param>
 <xsl:with-param name="val" select="@poll" />
</xsl:call-template>

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> dtmfthreshold=</xsl:with-param>
 <xsl:with-param name="val" select="@dtmfthreshold" />
</xsl:call-template>

<xsl:text>
</xsl:text>

</xsl:template>

</xsl:stylesheet>
