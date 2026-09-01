#pragma once

#include <afxwin.h>

// Derive every floating-window font from the host-selected font. This keeps
// the face, charset and rasterization settings consistent while allowing
// individual controls to use different sizes and weights.
inline bool CreateStockFont(CFont& target, CDC& dc, int pixelHeight, LONG weight = FW_DONTCARE)
{
	LOGFONT lf{};
	HFONT currentFont = static_cast<HFONT>(::GetCurrentObject(dc.GetSafeHdc(), OBJ_FONT));
	if (currentFont == nullptr || ::GetObject(currentFont, sizeof(lf), &lf) != sizeof(lf))
	{
		lf.lfCharSet = DEFAULT_CHARSET;
		lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
		lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
		lf.lfQuality = DEFAULT_QUALITY;
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
		_tcscpy_s(lf.lfFaceName, _T("Segoe UI"));
	}

	// The host font has a larger visual body than the old hard-coded fonts.
	// Compensate once here so all derived controls keep the previous density.
	int adjustedHeight = pixelHeight * 85 / 100;
	lf.lfHeight = -max(1, adjustedHeight);
	lf.lfWidth = 0; // Let GDI preserve the font's natural aspect ratio.
	lf.lfEscapement = 0;
	lf.lfOrientation = 0;
	if (weight != FW_DONTCARE)
		lf.lfWeight = weight;

	return target.CreateFontIndirect(&lf) != FALSE;
}
