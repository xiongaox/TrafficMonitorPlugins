#include "pch.h"
#include "Icons/Icons.h"
#include "Icons/IconsData.h"

namespace
{
	const IconsData::Icon& GetIcon(Icons::Id id)
	{
		using namespace IconsData;
		static const Icon x = { kX, _countof(kX), nullptr, 0, nullptr, 0 };
		static const Icon check = { kCheck, _countof(kCheck), nullptr, 0, nullptr, 0 };
		static const Icon plus = { kPlus, _countof(kPlus), nullptr, 0, nullptr, 0 };
		static const Icon trash2 = { kTrash2, _countof(kTrash2), nullptr, 0, nullptr, 0 };
		static const Icon pencil = { kPencil, _countof(kPencil), nullptr, 0, nullptr, 0 };
		static const Icon arrowUp = { kArrowUp, _countof(kArrowUp), nullptr, 0, nullptr, 0 };
		static const Icon arrowDown = { kArrowDown, _countof(kArrowDown), nullptr, 0, nullptr, 0 };
		static const Icon arrowUpDown = { kArrowUpDown, _countof(kArrowUpDown), nullptr, 0, nullptr, 0 };
		static const Icon refreshCw = { kRefreshCw, _countof(kRefreshCw), nullptr, 0, nullptr, 0 };
		static const Icon chevronUp = { kChevronUp, _countof(kChevronUp), nullptr, 0, nullptr, 0 };
		static const Icon chevronDown = { kChevronDown, _countof(kChevronDown), nullptr, 0, nullptr, 0 };
		static const Icon chevronsUp = { kChevronsUp, _countof(kChevronsUp), nullptr, 0, nullptr, 0 };
		static const Icon chevronsDown = { kChevronsDown, _countof(kChevronsDown), nullptr, 0, nullptr, 0 };
		static const Icon panelLeftOpen = { kPanelLeftOpen, _countof(kPanelLeftOpen), nullptr, 0, nullptr, 0 };
		static const Icon panelLeftClose = { kPanelLeftClose, _countof(kPanelLeftClose), nullptr, 0, nullptr, 0 };
		static const Icon solidTriangleUp = { nullptr, 0, nullptr, 0, kSolidTriangleUp, _countof(kSolidTriangleUp) };
		static const Icon solidTriangleDown = { nullptr, 0, nullptr, 0, kSolidTriangleDown, _countof(kSolidTriangleDown) };

		switch (id)
		{
		case Icons::Id::X: return x;
		case Icons::Id::Check: return check;
		case Icons::Id::Plus: return plus;
		case Icons::Id::Trash2: return trash2;
		case Icons::Id::Pencil: return pencil;
		case Icons::Id::ArrowUp: return arrowUp;
		case Icons::Id::ArrowDown: return arrowDown;
		case Icons::Id::ArrowUpDown: return arrowUpDown;
		case Icons::Id::RefreshCw: return refreshCw;
		case Icons::Id::ChevronUp: return chevronUp;
		case Icons::Id::ChevronDown: return chevronDown;
		case Icons::Id::ChevronsUp: return chevronsUp;
		case Icons::Id::ChevronsDown: return chevronsDown;
		case Icons::Id::PanelLeftOpen: return panelLeftOpen;
		case Icons::Id::PanelLeftClose: return panelLeftClose;
		case Icons::Id::SolidTriangleUp: return solidTriangleUp;
		case Icons::Id::SolidTriangleDown: return solidTriangleDown;
		}
		return x;
	}
}

void Icons::Draw(Gdiplus::Graphics& graphics, Id id, const Gdiplus::RectF& bounds,
	COLORREF color, BYTE alpha, float strokeWidth)
{
	if (bounds.Width <= 0.0f || bounds.Height <= 0.0f)
		return;

	const float scale = min(bounds.Width, bounds.Height) / 24.0f;
	const float offsetX = bounds.X + (bounds.Width - 24.0f * scale) / 2.0f;
	const float offsetY = bounds.Y + (bounds.Height - 24.0f * scale) / 2.0f;
	const Gdiplus::Color strokeColor(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
	const IconsData::Icon& icon = GetIcon(id);

	std::vector<Gdiplus::PointF> points;

	for (int polygonIndex = 0; polygonIndex < icon.polygonCount; ++polygonIndex)
	{
		const IconsData::Polygon& polygon = icon.polygons[polygonIndex];
		if (polygon.pointCount < 3)
			continue;

		points.clear();
		points.reserve(polygon.pointCount);
		for (int pointIndex = 0; pointIndex < polygon.pointCount; ++pointIndex)
		{
			const IconsData::Point& point = polygon.points[pointIndex];
			points.emplace_back(offsetX + point.x * scale, offsetY + point.y * scale);
		}
		Gdiplus::SolidBrush brush(strokeColor);
		graphics.FillPolygon(&brush, points.data(), static_cast<INT>(points.size()));
	}

	if (icon.strokeCount == 0 && icon.circleCount == 0)
		return;

	Gdiplus::Pen pen(strokeColor, max(1.0f, strokeWidth * scale));
	pen.SetStartCap(Gdiplus::LineCapRound);
	pen.SetEndCap(Gdiplus::LineCapRound);
	pen.SetLineJoin(Gdiplus::LineJoinRound);

	for (int strokeIndex = 0; strokeIndex < icon.strokeCount; ++strokeIndex)
	{
		const IconsData::Stroke& stroke = icon.strokes[strokeIndex];
		if (stroke.pointCount < 2)
			continue;

		points.clear();
		points.reserve(stroke.pointCount);
		for (int pointIndex = 0; pointIndex < stroke.pointCount; ++pointIndex)
		{
			const IconsData::Point& point = stroke.points[pointIndex];
			points.emplace_back(offsetX + point.x * scale, offsetY + point.y * scale);
		}
		graphics.DrawLines(&pen, points.data(), static_cast<INT>(points.size()));
	}

	for (int circleIndex = 0; circleIndex < icon.circleCount; ++circleIndex)
	{
		const IconsData::Circle& circle = icon.circles[circleIndex];
		const float radius = circle.radius * scale;
		graphics.DrawEllipse(&pen, offsetX + (circle.cx - circle.radius) * scale,
			offsetY + (circle.cy - circle.radius) * scale, radius * 2.0f, radius * 2.0f);
	}
}
