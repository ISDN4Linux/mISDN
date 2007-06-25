<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" version="1.0" encoding="UTF-8" indent="yes"/>

<!--
	Module: mISDN_debugtool
	Options: port=<number>
-->
<xsl:template name="MISDNdebugtoolmodule">

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> PORT=</xsl:with-param>
 <xsl:with-param name="val" select="@port" />
</xsl:call-template>

<xsl:text>
</xsl:text>

</xsl:template>

</xsl:stylesheet>
