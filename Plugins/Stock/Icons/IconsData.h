#pragma once

// Reviewed geometry derived from the SVG sources in Icons/lucide and Icons/custom.
// The coordinates are in Lucide's 24 x 24 viewBox. gen_icons.py validates that the
// source set stays inside the renderer's supported subset; it does not emit this
// file, so update the data below by hand after changing the source SVG set. Sources
// that use arc commands (pencil, refresh-cw, trash-2) are approximated as polylines
// here, because the renderer draws straight segments only.

namespace IconsData
{
	struct Point
	{
		float x;
		float y;
	};

	struct Stroke
	{
		const Point* points;
		int pointCount;
	};

	struct Circle
	{
		float cx;
		float cy;
		float radius;
	};

	struct Polygon
	{
		const Point* points;
		int pointCount;
	};

	struct Icon
	{
		const Stroke* strokes;
		int strokeCount;
		const Circle* circles;
		int circleCount;
		const Polygon* polygons;
		int polygonCount;
	};

#define ICON_POINTS(name, ...) \
	static const Point name[] = { __VA_ARGS__ }
#define ICON_STROKES(name, ...) \
	static const Stroke name[] = { __VA_ARGS__ }
#define ICON_POLYGONS(name, ...) \
	static const Polygon name[] = { __VA_ARGS__ }

	ICON_POINTS(kX0, { 18, 6 }, { 6, 18 });
	ICON_POINTS(kX1, { 6, 6 }, { 18, 18 });
	ICON_STROKES(kX, { kX0, _countof(kX0) }, { kX1, _countof(kX1) });

	ICON_POINTS(kCheck0, { 20, 6 }, { 9, 17 }, { 4, 12 });
	ICON_STROKES(kCheck, { kCheck0, _countof(kCheck0) });

	ICON_POINTS(kPlus0, { 5, 12 }, { 19, 12 });
	ICON_POINTS(kPlus1, { 12, 5 }, { 12, 19 });
	ICON_STROKES(kPlus, { kPlus0, _countof(kPlus0) }, { kPlus1, _countof(kPlus1) });

	ICON_POINTS(kTrash20, { 10, 11 }, { 10, 17 });
	ICON_POINTS(kTrash21, { 14, 11 }, { 14, 17 });
	ICON_POINTS(kTrash22, { 19, 6 }, { 19, 20 }, { 18.4142f, 21.4142f }, { 17, 22 }, { 7, 22 }, { 5.5858f, 21.4142f }, { 5, 20 }, { 5, 6 });
	ICON_POINTS(kTrash23, { 3, 6 }, { 21, 6 });
	ICON_POINTS(kTrash24, { 8, 6 }, { 8, 4 }, { 8.5858f, 2.5858f }, { 10, 2 }, { 14, 2 }, { 15.4142f, 2.5858f }, { 16, 4 }, { 16, 6 });
	ICON_STROKES(kTrash2, { kTrash20, _countof(kTrash20) }, { kTrash21, _countof(kTrash21) }, { kTrash22, _countof(kTrash22) }, { kTrash23, _countof(kTrash23) }, { kTrash24, _countof(kTrash24) });

	ICON_POINTS(kPencil0, { 21.174f, 6.812f }, { 17.188f, 2.825f }, { 3.842f, 16.174f }, { 3.342f, 17.004f }, { 2.021f, 21.356f }, { 2.644f, 21.978f }, { 6.997f, 20.658f }, { 7.827f, 20.161f }, { 21.174f, 6.812f });
	ICON_POINTS(kPencil1, { 15, 5 }, { 19, 9 });
	ICON_STROKES(kPencil, { kPencil0, _countof(kPencil0) }, { kPencil1, _countof(kPencil1) });

	ICON_POINTS(kArrowUp0, { 5, 12 }, { 12, 5 }, { 19, 12 });
	ICON_POINTS(kArrowUp1, { 12, 19 }, { 12, 5 });
	ICON_STROKES(kArrowUp, { kArrowUp0, _countof(kArrowUp0) }, { kArrowUp1, _countof(kArrowUp1) });

	ICON_POINTS(kArrowDown0, { 12, 5 }, { 12, 19 });
	ICON_POINTS(kArrowDown1, { 19, 12 }, { 12, 19 }, { 5, 12 });
	ICON_STROKES(kArrowDown, { kArrowDown0, _countof(kArrowDown0) }, { kArrowDown1, _countof(kArrowDown1) });

	ICON_POINTS(kArrowUpDown0, { 21, 16 }, { 17, 20 }, { 13, 16 });
	ICON_POINTS(kArrowUpDown1, { 17, 20 }, { 17, 4 });
	ICON_POINTS(kArrowUpDown2, { 3, 8 }, { 7, 4 }, { 11, 8 });
	ICON_POINTS(kArrowUpDown3, { 7, 4 }, { 7, 20 });
	ICON_STROKES(kArrowUpDown, { kArrowUpDown0, _countof(kArrowUpDown0) }, { kArrowUpDown1, _countof(kArrowUpDown1) }, { kArrowUpDown2, _countof(kArrowUpDown2) }, { kArrowUpDown3, _countof(kArrowUpDown3) });

	ICON_POINTS(kRefreshCw0, { 3, 12 }, { 3.977f, 7.757f }, { 6.75f, 4.246f }, { 10.745f, 3.075f }, { 14.717f, 3.398f }, { 18.74f, 5.74f }, { 21, 8 });
	ICON_POINTS(kRefreshCw1, { 21, 3 }, { 21, 8 }, { 16, 8 });
	ICON_POINTS(kRefreshCw2, { 21, 12 }, { 20.023f, 16.243f }, { 17.25f, 19.754f }, { 13.255f, 20.925f }, { 9.283f, 20.602f }, { 5.26f, 18.26f }, { 3, 16 });
	ICON_POINTS(kRefreshCw3, { 8, 16 }, { 3, 16 }, { 3, 21 });
	ICON_STROKES(kRefreshCw, { kRefreshCw0, _countof(kRefreshCw0) }, { kRefreshCw1, _countof(kRefreshCw1) }, { kRefreshCw2, _countof(kRefreshCw2) }, { kRefreshCw3, _countof(kRefreshCw3) });

	ICON_POINTS(kChevronUp0, { 18, 15 }, { 12, 9 }, { 6, 15 });
	ICON_STROKES(kChevronUp, { kChevronUp0, _countof(kChevronUp0) });

	ICON_POINTS(kChevronDown0, { 6, 9 }, { 12, 15 }, { 18, 9 });
	ICON_STROKES(kChevronDown, { kChevronDown0, _countof(kChevronDown0) });

	ICON_POINTS(kChevronsUp0, { 17, 11 }, { 12, 6 }, { 7, 11 });
	ICON_POINTS(kChevronsUp1, { 17, 18 }, { 12, 13 }, { 7, 18 });
	ICON_STROKES(kChevronsUp, { kChevronsUp0, _countof(kChevronsUp0) }, { kChevronsUp1, _countof(kChevronsUp1) });

	ICON_POINTS(kChevronsDown0, { 7, 6 }, { 12, 11 }, { 17, 6 });
	ICON_POINTS(kChevronsDown1, { 7, 13 }, { 12, 18 }, { 17, 13 });
	ICON_STROKES(kChevronsDown, { kChevronsDown0, _countof(kChevronsDown0) }, { kChevronsDown1, _countof(kChevronsDown1) });

	ICON_POINTS(kPanelLeftOpen0, { 3, 3 }, { 21, 3 }, { 21, 21 }, { 3, 21 }, { 3, 3 });
	ICON_POINTS(kPanelLeftOpen1, { 9, 3 }, { 9, 21 });
	ICON_POINTS(kPanelLeftOpen2, { 14, 9 }, { 17, 12 }, { 14, 15 });
	ICON_STROKES(kPanelLeftOpen, { kPanelLeftOpen0, _countof(kPanelLeftOpen0) }, { kPanelLeftOpen1, _countof(kPanelLeftOpen1) }, { kPanelLeftOpen2, _countof(kPanelLeftOpen2) });

	ICON_POINTS(kPanelLeftClose0, { 3, 3 }, { 21, 3 }, { 21, 21 }, { 3, 21 }, { 3, 3 });
	ICON_POINTS(kPanelLeftClose1, { 9, 3 }, { 9, 21 });
	ICON_POINTS(kPanelLeftClose2, { 16, 15 }, { 13, 12 }, { 16, 9 });
	ICON_STROKES(kPanelLeftClose, { kPanelLeftClose0, _countof(kPanelLeftClose0) }, { kPanelLeftClose1, _countof(kPanelLeftClose1) }, { kPanelLeftClose2, _countof(kPanelLeftClose2) });

	// Solid sort indicators. These are the only filled icons in the set; their
	// sources live in Icons/custom rather than Icons/lucide.
	ICON_POINTS(kSolidTriangleUp0, { 12, 5 }, { 4, 19 }, { 20, 19 });
	ICON_POLYGONS(kSolidTriangleUp, { kSolidTriangleUp0, _countof(kSolidTriangleUp0) });

	ICON_POINTS(kSolidTriangleDown0, { 4, 5 }, { 20, 5 }, { 12, 19 });
	ICON_POLYGONS(kSolidTriangleDown, { kSolidTriangleDown0, _countof(kSolidTriangleDown0) });

#undef ICON_POINTS
#undef ICON_STROKES
#undef ICON_POLYGONS
}
