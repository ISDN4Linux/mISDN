<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" version="1.0" encoding="UTF-8" indent="yes"/>

<xsl:template name="if-set">
 <xsl:param name="prefix"></xsl:param>
 <xsl:param name="val"></xsl:param>
 <xsl:param name="val-default"></xsl:param>
 <xsl:choose>
  <xsl:when test="$val!=''">
   <xsl:value-of select="concat($prefix,$val)" />
  </xsl:when>
  <xsl:otherwise>
   <xsl:if test="$val-default!=''">
    <xsl:value-of select="concat($prefix,$val-default)" />
   </xsl:if>
  </xsl:otherwise>
 </xsl:choose>
</xsl:template>

<xsl:template name="if-set-match">
 <xsl:param name="prefix"></xsl:param>
 <xsl:param name="val"></xsl:param>
 <xsl:param name="val-default"></xsl:param>
 <xsl:param name="val-true">0</xsl:param>
 <xsl:param name="val-false">0</xsl:param>
 <xsl:param name="match-true">yes</xsl:param>
 <xsl:param name="match-false">no</xsl:param>
 <xsl:choose>
  <xsl:when test="$val=$match-true">
   <xsl:value-of select="concat($prefix,$val-true)" />
  </xsl:when>
  <xsl:when test="$val=$match-false">
   <xsl:value-of select="concat($prefix,$val-false)" />
  </xsl:when>
  <xsl:otherwise>
   <xsl:if test="$val-default!=''">
    <xsl:value-of select="concat($prefix,$val-default)" />
   </xsl:if>
  </xsl:otherwise>
 </xsl:choose>
</xsl:template>

<xsl:template name="if-match">
 <xsl:param name="val">no</xsl:param>
 <xsl:param name="val-default">0</xsl:param>
 <xsl:param name="val-true">0</xsl:param>
 <xsl:param name="val-false">0</xsl:param>
 <xsl:param name="match-true">yes</xsl:param>
 <xsl:param name="match-false">no</xsl:param>
 <xsl:choose>
  <xsl:when test="$val=$match-true">
   <xsl:value-of select="$val-true" />
  </xsl:when>
  <xsl:when test="$val=$match-false">
   <xsl:value-of select="$val-false" />
  </xsl:when>
  <xsl:otherwise>
   <xsl:value-of select="$val-default" />
  </xsl:otherwise>
 </xsl:choose>
</xsl:template>

</xsl:stylesheet>
