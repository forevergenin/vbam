// xImalpha.cpp : Alpha channel functions
/* 07/08/2001 v1.00 - Davide Pizzolato - www.xdp.it
 * CxImage version 5.99c 17/Oct/2004
 */

#include "ximage.h"

#if CXIMAGE_SUPPORT_ALPHA

////////////////////////////////////////////////////////////////////////////////
/**
 * \sa AlphaSetMax
 */
BYTE CxImage::AlphaGetMax() const
{
	return info.nAlphaMax;
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Sets global Alpha (opacity) value applied to the whole image,
 * valid only for painting functions.
 * \param nAlphaMax: can be from 0 to 255
 */
void CxImage::AlphaSetMax(BYTE nAlphaMax)
{
	info.nAlphaMax=nAlphaMax;
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Checks if the image has a valid alpha channel.
 */
bool CxImage::AlphaIsValid()
{
	return pAlpha!=0;
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Enables the alpha palette, so the Draw() function changes its behavior.
 */
void CxImage::AlphaPaletteEnable(bool enable)
{
	info.bAlphaPaletteEnabled=enable;
}
////////////////////////////////////////////////////////////////////////////////
/**
 * True if the alpha palette is enabled for painting.
 */
bool CxImage::AlphaPaletteIsEnabled()
{
	return info.bAlphaPaletteEnabled;
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Inverts the alpha channel.
 */
void CxImage::AlphaClear()
{
	if (pAlpha)	memset(pAlpha,0,head.biWidth * head.biHeight);
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Sets the alpha level for the whole image
 */
void CxImage::AlphaSet(BYTE level)
{
	if (pAlpha)	memset(pAlpha,level,head.biWidth * head.biHeight);
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Allocates an empty (opaque) alpha channel.
 */
void CxImage::AlphaCreate()
{
	if (pAlpha==NULL) {
		pAlpha = (BYTE*)malloc(head.biWidth * head.biHeight);
		if (pAlpha) memset(pAlpha,255,head.biWidth * head.biHeight);
	}
}
////////////////////////////////////////////////////////////////////////////////
void CxImage::AlphaDelete()
{
	if (pAlpha) { free(pAlpha); pAlpha=0; }
}
////////////////////////////////////////////////////////////////////////////////
void CxImage::AlphaInvert()
{
	if (pAlpha) {
		BYTE *iSrc=pAlpha;
		long n=head.biHeight*head.biWidth;
		for(long i=0; i < n; i++){
			*iSrc=(BYTE)~(*(iSrc));
			iSrc++;
		}
	}
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Imports an existing alpa channel from another image with the same width and height.
 */
bool CxImage::AlphaCopy(CxImage &from)
{
	if (from.pAlpha == NULL || head.biWidth != from.head.biWidth || head.biHeight != from.head.biHeight) return false;
	if (pAlpha==NULL) pAlpha = (BYTE*)malloc(head.biWidth * head.biHeight);
	if (pAlpha==NULL) return false;
	memcpy(pAlpha,from.pAlpha,head.biWidth * head.biHeight);
	info.nAlphaMax=from.info.nAlphaMax;
	return true;
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Creates the alpha channel from a gray scale image.
 */
bool CxImage::AlphaSet(CxImage &from)
{
	if (!from.IsGrayScale() || head.biWidth != from.head.biWidth || head.biHeight != from.head.biHeight) return false;
	if (pAlpha==NULL) pAlpha = (BYTE*)malloc(head.biWidth * head.biHeight);
	BYTE* src = from.info.pImage;
	BYTE* dst = pAlpha;
	if (src==NULL || dst==NULL) return false;
	for (long y=0; y<head.biHeight; y++){
		memcpy(dst,src,head.biWidth);
		dst += head.biWidth;
		src += from.info.dwEffWidth;
	}
	return true;
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Sets the alpha level for a single pixel
 */
void CxImage::AlphaSet(const long x,const long y,const BYTE level)
{
	if (pAlpha && IsInside(x,y)) pAlpha[x+y*head.biWidth]=level;
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Gets the alpha level for a single pixel
 */
BYTE CxImage::AlphaGet(const long x,const long y)
{
	if (pAlpha && IsInside(x,y)) return pAlpha[x+y*head.biWidth];
	return 0;
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Returns pointer to alpha data for pixel (x,y).
 *
 * \author ***bd*** 2.2004
 */
BYTE* CxImage::AlphaGetPointer(const long x,const long y)
{
	if (pAlpha && IsInside(x,y)) return pAlpha+x+y*head.biWidth;
	return 0;
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Get alpha value without boundscheck (a bit faster). Pixel must be inside the image.
 *
 * \author ***bd*** 2.2004
 */
BYTE CxImage::BlindAlphaGet(const long x,const long y)
{
#ifdef _DEBUG
	if (!IsInside(x,y) || (pAlpha==0)) throw 0;
#endif
	return pAlpha[x+y*head.biWidth];
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Resets the alpha palette
 */
void CxImage::AlphaPaletteClear()
{
	RGBQUAD c;
	for(WORD ip=0; ip<head.biClrUsed;ip++){
		c=GetPaletteColor((BYTE)ip);
		c.rgbReserved=0;
		SetPaletteColor((BYTE)ip,c);
	}
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Checks if the image has a valid alpha palette.
 */
bool CxImage::AlphaPaletteIsValid()
{
	RGBQUAD c;
	for(WORD ip=0; ip<head.biClrUsed;ip++){
		c=GetPaletteColor((BYTE)ip);
		if (c.rgbReserved != 0) return true;
	}
	return false;
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Blends the alpha channel and the alpha palette with the pixels. The result is a 24 bit image.
 * The background color can be selected using SetTransColor().
 */
void CxImage::AlphaStrip()
{
	bool bAlphaPaletteIsValid = AlphaPaletteIsValid();
	bool bAlphaIsValid = AlphaIsValid();
	if (!(bAlphaIsValid || bAlphaPaletteIsValid)) return;
	RGBQUAD c;
	long a, a1;
	if (head.biBitCount==24){
		for(long y=0; y<head.biHeight; y++){
			for(long x=0; x<head.biWidth; x++){
				c=GetPixelColor(x,y);
				if (bAlphaIsValid) a=(AlphaGet(x,y)*info.nAlphaMax)/255; else a=info.nAlphaMax;
				a1 = 255-a;
				c.rgbBlue = (BYTE)((c.rgbBlue * a + a1 * info.nBkgndColor.rgbBlue)/255);
				c.rgbGreen = (BYTE)((c.rgbGreen * a + a1 * info.nBkgndColor.rgbGreen)/255);
				c.rgbRed = (BYTE)((c.rgbRed * a + a1 * info.nBkgndColor.rgbRed)/255);
				SetPixelColor(x,y,c);
			}
		}
		AlphaDelete();
	} else {
		CxImage tmp(head.biWidth,head.biHeight,24);
		if (!tmp.IsValid()) return;
		for(long y=0; y<head.biHeight; y++){
			for(long x=0; x<head.biWidth; x++){
				c=GetPixelColor(x,y);
				if (bAlphaIsValid) a=(AlphaGet(x,y)*info.nAlphaMax)/255; else a=info.nAlphaMax;
				if (bAlphaPaletteIsValid) a=(c.rgbReserved*a)/255;
				a1 = 255-a;
				c.rgbBlue = (BYTE)((c.rgbBlue * a + a1 * info.nBkgndColor.rgbBlue)/255);
				c.rgbGreen = (BYTE)((c.rgbGreen * a + a1 * info.nBkgndColor.rgbGreen)/255);
				c.rgbRed = (BYTE)((c.rgbRed * a + a1 * info.nBkgndColor.rgbRed)/255);
				tmp.SetPixelColor(x,y,c);
			}
		}
		Transfer(tmp);
	}
	return;
}
////////////////////////////////////////////////////////////////////////////////
bool CxImage::AlphaFlip()
{
	if (!pAlpha) return false;
	BYTE* pAlpha2 = (BYTE*)malloc(head.biWidth * head.biHeight);
	if (!pAlpha2) return false;
	BYTE *iSrc,*iDst;
	iSrc=pAlpha + (head.biHeight-1)*head.biWidth;
	iDst=pAlpha2;
    for(long y=0; y < head.biHeight; y++){
		memcpy(iDst,iSrc,head.biWidth);
		iSrc-=head.biWidth;
		iDst+=head.biWidth;
	}
	free(pAlpha);
	pAlpha=pAlpha2;
	return true;
}
////////////////////////////////////////////////////////////////////////////////
bool CxImage::AlphaMirror()
{
	if (!pAlpha) return false;
	BYTE* pAlpha2 = (BYTE*)malloc(head.biWidth * head.biHeight);
	if (!pAlpha2) return false;
	BYTE *iSrc,*iDst;
	long wdt=head.biWidth-1;
	iSrc=pAlpha + wdt;
	iDst=pAlpha2;
	for(long y=0; y < head.biHeight; y++){
		for(long x=0; x <= wdt; x++)
			*(iDst+x)=*(iSrc-x);
		iSrc+=head.biWidth;
		iDst+=head.biWidth;
	}
	free(pAlpha);
	pAlpha=pAlpha2;
	return true;
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Exports the alpha channel in a 8bpp grayscale image.
 */
bool CxImage::AlphaSplit(CxImage *dest)
{
	if (!pAlpha || !dest) return false;

	CxImage tmp(head.biWidth,head.biHeight,8);
	if (!tmp.IsValid()) return false;

	for(long y=0; y<head.biHeight; y++){
		for(long x=0; x<head.biWidth; x++){
			tmp.SetPixelIndex(x,y,pAlpha[x+y*head.biWidth]);
		}
	}

	tmp.SetGrayPalette();
	dest->Transfer(tmp);

	return true;
}
////////////////////////////////////////////////////////////////////////////////
/**
 * Exports the alpha palette channel in a 8bpp grayscale image.
 */
bool CxImage::AlphaPaletteSplit(CxImage *dest)
{
	if (!AlphaPaletteIsValid() || !dest) return false;

	CxImage tmp(head.biWidth,head.biHeight,8);
	if (!tmp.IsValid()) return false;

	for(long y=0; y<head.biHeight; y++){
		for(long x=0; x<head.biWidth; x++){
			tmp.SetPixelIndex(x,y,GetPixelColor(x,y).rgbReserved);
		}
	}

	tmp.SetGrayPalette();
	dest->Transfer(tmp);

	return true;
}
////////////////////////////////////////////////////////////////////////////////
#endif //CXIMAGE_SUPPORT_ALPHA
