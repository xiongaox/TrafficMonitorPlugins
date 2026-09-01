#pragma once

#include <afxwin.h>
#include "DataManager.h"

// Derive every floating-window font from the host-selected font. This keeps
// the face, charset and rasterization settings consistent while allowing
// individual controls to use different sizes and weights. The size scales
// proportionally with the host font size (referenced against 9pt at 96 DPI),
// so changing TrafficMonitor's display font resizes these fonts as well.
inline bool CreateStockFont(CFont& target, CDC& dc, int pixelHeight, LONG weight = FW_DONTCARE)
{
	LOGFONT lf{};
	if (g_data.HasHostFont())
	{
		lf = g_data.GetHostLogFont();
	}
	else
	{
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
	}

	// Density compensation for the host font's larger visual body.
	// 85% matched the old ~9pt look; 104% raises the baseline to ~11pt
	// (85% * 11/9 ≈ 104%) while keeping every derived font on one scale.
	int adjustedHeight = pixelHeight * 104 / 100;
	// 按主机字号等比缩放，保证字体统一后字号仍跟随主机字体设置缩放
	adjustedHeight = adjustedHeight * g_data.GetFontScalePercent() / 100;
	lf.lfHeight = -max(1, adjustedHeight);
	lf.lfWidth = 0; // Let GDI preserve the font's natural aspect ratio.
	lf.lfEscapement = 0;
	lf.lfOrientation = 0;
	if (weight != FW_DONTCARE)
		lf.lfWeight = weight;

	return target.CreateFontIndirect(&lf) != FALSE;
}
