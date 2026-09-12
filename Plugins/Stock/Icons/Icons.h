#pragma once

#include <gdiplus.h>

namespace Icons
{
	enum class Id
	{
		X,
		Check,
		Plus,
		Trash2,
		Pencil,
		ArrowUp,
		ArrowDown,
		ArrowUpDown,
		RefreshCw,
		ChevronUp,
		ChevronDown,
		ChevronsUp,
		ChevronsDown,
		PanelLeftOpen,
		PanelLeftClose,
		Settings,
		SolidTriangleUp,
		SolidTriangleDown,
	};

	// Draws a Lucide 24x24 stroke icon. Geometry is sourced from the versioned
	// SVG files in Icons/lucide; callers only provide placement and UI state color.
	void Draw(Gdiplus::Graphics& graphics, Id id, const Gdiplus::RectF& bounds,
		COLORREF color, BYTE alpha = 255, float strokeWidth = 2.0f);
}
