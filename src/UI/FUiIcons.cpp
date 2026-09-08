#include "FUiIcons.h"
#include "FUiScale.h"
#include "FUiTheme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
    // 文件夹：琥珀色，前后两片错开一点做出层次
    constexpr ImU32 kFolderBack  = IM_COL32(190, 134, 40, 255);
    constexpr ImU32 kFolderFront = IM_COL32(232, 176, 66, 255);

    // 文件：白纸 + 灰描边，浅色主题下比灰底更容易分辨
    constexpr ImU32 kFileFill   = IM_COL32(252, 252, 253, 255);
    constexpr ImU32 kFileStroke = IM_COL32(146, 155, 168, 255);
    constexpr ImU32 kFileFold   = IM_COL32(198, 205, 216, 255);

    // 忙碌遮罩：压暗底 + 白卡片。转圈颜色由主题的“按钮与选择色”提供。
    constexpr ImU32 kScrim        = IM_COL32(17, 22, 30, 104);
    constexpr ImU32 kCardFill     = IM_COL32(252, 253, 254, 250);
    constexpr ImU32 kCardBorder   = IM_COL32(196, 206, 219, 255);
    constexpr ImU32 kLabelText    = IM_COL32(42, 50, 62, 255);
    constexpr ImU32 kDetailText   = IM_COL32(120, 131, 147, 255);
    constexpr float kSpinnerTrackAlpha = 48.0f / 255.0f;

    // 朝向图标：按钮底是深青强调色，所以图标一律白色
    constexpr ImU32 kGlyphColor = IM_COL32(255, 255, 255, 255);
    constexpr float kCloseGlyphInsetRatio = 0.25f;
    constexpr float kCloseGlyphStrokeRatio = 0.0625f;
    constexpr float kExifGlyphInsetRatio = 0.12f;
    constexpr float kExifGlyphRoundingRatio = 0.12f;
    constexpr float kExifGlyphStrokeRatio = 0.0625f;
    constexpr float kExifGlyphCenterRatio = 0.5f;
    constexpr float kExifGlyphDotYRatio = 0.32f;
    constexpr float kExifGlyphStemTopRatio = 0.47f;
    constexpr float kExifGlyphStemBottomRatio = 0.73f;

    // 文件选中态读取选择项主题色，但保留原透明度，避免在浅色面板里淡到难以辨认。
    constexpr float kFileSelectedAlpha = 0.82f;
    constexpr float kFileSelectedHoveredAlpha = 0.90f;
    constexpr float kFileSelectedActiveAlpha = 0.96f;

    // 对比图使用固定琥珀色，与主图/普通选中的主题色形成稳定的身份区分。
    const ImVec4 kFileCompared = ImVec4(0.824f, 0.478f, 0.118f, 0.84f);
    const ImVec4 kFileComparedHovered = ImVec4(0.749f, 0.392f, 0.075f, 0.92f);
    const ImVec4 kFileComparedActive = ImVec4(0.651f, 0.302f, 0.043f, 0.97f);

    /// 同一文件同时是普通选中项与对比图时，用主题选择色侧边条保留双重状态。
    constexpr float kDualFileHighlightAccentWidth = 4.0f;

    constexpr float kPi = 3.14159265358979323846f;

    /// 圆弧的分段数。再多看不出区别，再少边缘会显出折线
    constexpr int32_t kSpinnerSegments = 40;

    /**
     * 两个镜像图标直接使用 1536×1024 参考图的测量坐标。
     *
     * 它们只在绘制时把紧包围盒等比映射到按钮正方形；原始锚点、曲线控制点和
     * 笔画宽度均不改写，避免优化另外四个图标时改变已经确认的镜像轮廓。
     */
    struct FSourcePoint
    {
        float X;
        float Y;
    };

    struct FSourceViewBox
    {
        float MinX;
        float MinY;
        float Width;
        float Height;
    };

    struct FSourceTransform
    {
        float ScaleX;
        float ScaleY;
        float TranslateX;
        float TranslateY;
    };

    struct FSourceMapper
    {
        ImVec2 Offset;
        float Scale;
    };

    struct FNormalizedPoint
    {
        float X;
        float Y;
    };

    struct FMirrorHorizontalPanelGeometry
    {
        FSourcePoint Start;
        FSourcePoint InnerTop;
        FSourcePoint InnerTopControl;
        FSourcePoint InnerTopEnd;
        FSourcePoint InnerBottomStart;
        FSourcePoint InnerBottomControl;
        FSourcePoint InnerBottomEnd;
        FSourcePoint OuterBottom;
        FSourcePoint OuterBottomControl;
        FSourcePoint OuterBottomEnd;
        FSourcePoint OuterTopEnd;
        FSourcePoint OuterTopControl;
        FSourcePoint ClosePoint;
    };

    struct FMirrorVerticalTrapezoidGeometry
    {
        FSourcePoint Start;
        FSourcePoint TopRight;
        FSourcePoint RightOuterControl;
        FSourcePoint RightOuter;
        FSourcePoint RightInner;
        FSourcePoint RightInnerControl;
        FSourcePoint BottomRight;
        FSourcePoint BottomLeft;
        FSourcePoint LeftInnerControl;
        FSourcePoint LeftInner;
        FSourcePoint LeftOuter;
        FSourcePoint LeftOuterControl;
        FSourcePoint ClosePoint;
    };

    enum class ERotationDirection
    {
        Clockwise,
        CounterClockwise
    };

    enum class ETileOrientation
    {
        Horizontal,
        Vertical
    };

    constexpr int32_t kRotationArcSegments = 24;
    constexpr int32_t kRoundedGeometryCurveSegments = 4;
    constexpr float kMinimumAntiAliasedStrokePixels = 1.0f;
    constexpr float kHalfTurnDegrees = 180.0f;
    constexpr float kNormalizedCenter = 0.5f;
    constexpr FSourceTransform kIdentitySourceTransform = { 1.0f, 1.0f, 0.0f, 0.0f };

    constexpr FSourceViewBox kMirrorHorizontalViewBox = { 135.0f, 109.0f, 312.0f, 338.0f };
    constexpr FSourceViewBox kMirrorVerticalViewBox = { 582.5f, 125.5f, 369.0f, 330.0f };

    constexpr float kMirrorHorizontalStrokeWidth = 21.75f;
    constexpr FSourcePoint kMirrorHorizontalOrigin = { 290.875f, 278.0f };
    constexpr FSourcePoint kMirrorHorizontalAxisStart = { 0.125f, -146.875f };
    constexpr FSourcePoint kMirrorHorizontalAxisEnd = { 0.125f, 147.5f };
    constexpr FMirrorHorizontalPanelGeometry kMirrorHorizontalPanel = {
        { -118.125f, -118.375f },
        { -50.5f, -67.125f },
        { -50.25f, -64.0f },
        { -48.0f, -55.75f },
        { -48.0f, 55.25f },
        { -47.5f, 62.0f },
        { -53.5f, 70.625f },
        { -116.125f, 117.875f },
        { -132.375f, 119.5f },
        { -134.0f, 105.75f },
        { -134.0f, -105.5f },
        { -130.625f, -118.625f },
        { -117.75f, -116.125f },
    };

    constexpr float kMirrorVerticalStrokeWidth = 20.5f;
    constexpr float kMirrorVerticalAxisStrokeWidth = 20.875f;
    constexpr FSourcePoint kMirrorVerticalOrigin = { 767.0f, 290.625f };
    constexpr FSourcePoint kMirrorVerticalAxisStart = { -149.875f, -0.875f };
    constexpr FSourcePoint kMirrorVerticalAxisEnd = { 150.5f, -0.875f };
    constexpr FMirrorVerticalTrapezoidGeometry kMirrorVerticalTrapezoid = {
        { -117.875f, -130.875f },
        { 118.375f, -130.875f },
        { 133.125f, -126.875f },
        { 129.0f, -112.25f },
        { 101.75f, -47.625f },
        { 94.75f, -38.5f },
        { 81.625f, -38.75f },
        { -82.75f, -38.75f },
        { -97.0f, -38.875f },
        { -103.0f, -52.75f },
        { -129.0f, -115.0f },
        { -130.0f, -130.125f },
        { -114.125f, -130.75f },
    };

    // 四个重设计图标使用同一 0..1 坐标系，保证镜像/旋转实例不会产生第二套手调参数。
    constexpr float kRotationStrokeRatio = 0.064f;
    constexpr FNormalizedPoint kRotationCenter = { 0.525f, 0.5f };
    constexpr float kRotationRadiusRatio = 0.43f;
    constexpr float kRotationArcStartDegrees = 29.0f;
    constexpr float kRotationArcEndDegrees = 323.0f;
    constexpr FNormalizedPoint kRotationArrowTip = { 0.87f, 0.25f };
    constexpr float kRotationArrowArmRatio = 0.18f;

    constexpr float kTileStrokeRatio = 0.0625f;
    constexpr FNormalizedPoint kTileFirstFrameCenter = { 0.26f, 0.5f };
    constexpr FNormalizedPoint kTileFrameRepeatOffset = { 0.48f, 0.0f };
    constexpr float kTileFrameHalfShortRatio = 0.15f;
    constexpr float kTileFrameHalfLongRatio = 0.30f;
    constexpr float kTileFrameCornerRatio = 0.085f;

    // 链条只维护右上半环，左下半环绕图标中心旋转 180° 生成。
    constexpr float kChainStrokeRatio = 0.0625f;
    constexpr FNormalizedPoint kChainStart = { 0.40f, 0.55f };
    constexpr FNormalizedPoint kChainInnerControl1 = { 0.48f, 0.64f };
    constexpr FNormalizedPoint kChainInnerControl2 = { 0.62f, 0.66f };
    constexpr FNormalizedPoint kChainInnerEnd = { 0.72f, 0.56f };
    constexpr FNormalizedPoint kChainOuterStart = { 0.83f, 0.45f };
    constexpr FNormalizedPoint kChainOuterControl1 = { 0.94f, 0.34f };
    constexpr FNormalizedPoint kChainOuterControl2 = { 0.94f, 0.20f };
    constexpr FNormalizedPoint kChainOuterEnd = { 0.84f, 0.11f };
    constexpr FNormalizedPoint kChainReturnControl1 = { 0.75f, 0.02f };
    constexpr FNormalizedPoint kChainReturnControl2 = { 0.61f, 0.04f };
    constexpr FNormalizedPoint kChainReturnEnd = { 0.52f, 0.13f };
    constexpr FNormalizedPoint kChainTail = { 0.45f, 0.20f };

    constexpr float kOverlayButtonHoveredAlpha = 0.16f;
    constexpr float kOverlayButtonActiveAlpha = 0.25f;
    constexpr float kSelectedButtonBorderAlpha = 0.78f;
    constexpr float kViewerButtonIconRatio = 0.80f;
    constexpr int32_t kOverlayButtonColorStackCount = 3;

    /**
     * ImDrawList 的普通线段端点是平的，缩到工具栏尺寸后容易显得尖锐。
     * 在线段端点补同半径的圆，得到稳定的圆头；折线每段都这样画，拐角也会自然变圆。
     */
    void DrawLineBody(
        ImDrawList* DrawList,
        const ImVec2& Start,
        const ImVec2& End,
        ImU32 Color,
        float Thickness)
    {
        const ImVec2 points[] = { Start, End };
        DrawList->AddPolyline(
            points,
            IM_ARRAYSIZE(points),
            Color,
            ImDrawFlags_None,
            Thickness);
    }

    void DrawRoundedLine(
        ImDrawList* DrawList,
        const ImVec2& Start,
        const ImVec2& End,
        ImU32 Color,
        float Thickness)
    {
        DrawLineBody(DrawList, Start, End, Color, Thickness);

        // ImGui 的抗锯齿线会把小于 1px 的线宽提升到 1px；端帽按同一有效线宽补圆。
        const float renderedThickness =
            std::max(Thickness, kMinimumAntiAliasedStrokePixels);
        const float radius = renderedThickness * 0.5f;
        DrawList->AddCircleFilled(Start, radius, Color);
        DrawList->AddCircleFilled(End, radius, Color);
    }

    void DrawRoundedPolyline(
        ImDrawList* DrawList,
        const ImVec2* Points,
        int32_t PointCount,
        bool bClosed,
        ImU32 Color,
        float Thickness)
    {
        if (!Points || PointCount < 2)
        {
            return;
        }

        const int32_t segmentCount = bClosed ? PointCount : PointCount - 1;

        for (int32_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
        {
            DrawLineBody(
                DrawList,
                Points[segmentIndex],
                Points[(segmentIndex + 1) % PointCount],
                Color,
                Thickness);
        }

        const float renderedThickness =
            std::max(Thickness, kMinimumAntiAliasedStrokePixels);
        const float radius = renderedThickness * 0.5f;

        // 每个节点只补一次圆，避免相邻线段的 AA 端帽重复叠加形成鼓点。
        for (int32_t pointIndex = 0; pointIndex < PointCount; ++pointIndex)
        {
            DrawList->AddCircleFilled(Points[pointIndex], radius, Color);
        }
    }

    /**
     * ImDrawList 的 PathStroke 固定使用斜接，无法选择 SVG 的 round linejoin。
     * 把已细分路径逐段画成胶囊线段，端帽与每个连接点都由同半径圆覆盖，不会留下尖刺。
     */
    void StrokeCurrentPathRounded(
        ImDrawList* DrawList,
        bool bClosed,
        ImU32 Color,
        float Thickness)
    {
        // AddCircleFilled 会复用 DrawList->_Path，因此先复制到持久 scratch；ImGui 本身也只在 UI 线程绘制。
        static ImVector<ImVec2> roundedStrokePathScratch;
        roundedStrokePathScratch.resize(DrawList->_Path.Size);

        for (int32_t pointIndex = 0; pointIndex < DrawList->_Path.Size; ++pointIndex)
        {
            roundedStrokePathScratch[pointIndex] = DrawList->_Path[pointIndex];
        }

        DrawList->PathClear();
        int32_t pointCount = roundedStrokePathScratch.Size;

        if (bClosed &&
            pointCount > 2 &&
            roundedStrokePathScratch[0].x == roundedStrokePathScratch[pointCount - 1].x &&
            roundedStrokePathScratch[0].y == roundedStrokePathScratch[pointCount - 1].y)
        {
            --pointCount;
        }

        DrawRoundedPolyline(
            DrawList,
            roundedStrokePathScratch.Data,
            pointCount,
            bClosed,
            Color,
            Thickness);
    }

    FSourceMapper CreateSourceMapper(
        const ImVec2& Pos,
        float Size,
        const FSourceViewBox& ViewBox)
    {
        const float scale = std::min(Size / ViewBox.Width, Size / ViewBox.Height);
        const float renderedWidth = ViewBox.Width * scale;
        const float renderedHeight = ViewBox.Height * scale;
        const ImVec2 offset(
            Pos.x + (Size - renderedWidth) * 0.5f - ViewBox.MinX * scale,
            Pos.y + (Size - renderedHeight) * 0.5f - ViewBox.MinY * scale);

        return { offset, scale };
    }

    FSourcePoint ApplySourceTransform(
        const FSourcePoint& Point,
        const FSourceTransform& Transform)
    {
        return {
            Point.X * Transform.ScaleX + Transform.TranslateX,
            Point.Y * Transform.ScaleY + Transform.TranslateY
        };
    }

    ImVec2 MapSourcePoint(
        const FSourceMapper& Mapper,
        const FSourcePoint& Point,
        const FSourceTransform& Transform = kIdentitySourceTransform)
    {
        const FSourcePoint transformed = ApplySourceTransform(Point, Transform);

        return ImVec2(
            Mapper.Offset.x + transformed.X * Mapper.Scale,
            Mapper.Offset.y + transformed.Y * Mapper.Scale);
    }

    float MapSourceStrokeWidth(
        const FSourceMapper& Mapper,
        float SourceStrokeWidth,
        const FSourceTransform& Transform = kIdentitySourceTransform)
    {
        // ImDrawList 只有标量线宽；非等比实例取面积保持的几何平均值，误差小于参考变换的 4%。
        const float transformScale =
            std::sqrt(std::fabs(Transform.ScaleX * Transform.ScaleY));

        return SourceStrokeWidth * Mapper.Scale * transformScale;
    }

    void DrawSourceRoundedLine(
        ImDrawList* DrawList,
        const FSourceMapper& Mapper,
        const FSourcePoint& Start,
        const FSourcePoint& End,
        float SourceStrokeWidth,
        const FSourceTransform& Transform = kIdentitySourceTransform)
    {
        DrawRoundedLine(
            DrawList,
            MapSourcePoint(Mapper, Start, Transform),
            MapSourcePoint(Mapper, End, Transform),
            kGlyphColor,
            MapSourceStrokeWidth(Mapper, SourceStrokeWidth, Transform));
    }

    void DrawMirrorHorizontalPanel(
        ImDrawList* DrawList,
        const FSourceMapper& Mapper,
        float ReflectionScaleX)
    {
        const FSourceTransform transform = {
            ReflectionScaleX,
            1.0f,
            kMirrorHorizontalOrigin.X,
            kMirrorHorizontalOrigin.Y
        };

        DrawList->PathClear();
        DrawList->PathLineTo(
            MapSourcePoint(Mapper, kMirrorHorizontalPanel.Start, transform));
        DrawList->PathLineTo(
            MapSourcePoint(Mapper, kMirrorHorizontalPanel.InnerTop, transform));
        DrawList->PathBezierQuadraticCurveTo(
            MapSourcePoint(
                Mapper,
                kMirrorHorizontalPanel.InnerTopControl,
                transform),
            MapSourcePoint(
                Mapper,
                kMirrorHorizontalPanel.InnerTopEnd,
                transform),
            kRoundedGeometryCurveSegments);
        DrawList->PathLineTo(
            MapSourcePoint(
                Mapper,
                kMirrorHorizontalPanel.InnerBottomStart,
                transform));
        DrawList->PathBezierQuadraticCurveTo(
            MapSourcePoint(
                Mapper,
                kMirrorHorizontalPanel.InnerBottomControl,
                transform),
            MapSourcePoint(
                Mapper,
                kMirrorHorizontalPanel.InnerBottomEnd,
                transform),
            kRoundedGeometryCurveSegments);
        DrawList->PathLineTo(
            MapSourcePoint(
                Mapper,
                kMirrorHorizontalPanel.OuterBottom,
                transform));
        DrawList->PathBezierQuadraticCurveTo(
            MapSourcePoint(
                Mapper,
                kMirrorHorizontalPanel.OuterBottomControl,
                transform),
            MapSourcePoint(
                Mapper,
                kMirrorHorizontalPanel.OuterBottomEnd,
                transform),
            kRoundedGeometryCurveSegments);
        DrawList->PathLineTo(
            MapSourcePoint(
                Mapper,
                kMirrorHorizontalPanel.OuterTopEnd,
                transform));
        DrawList->PathBezierQuadraticCurveTo(
            MapSourcePoint(
                Mapper,
                kMirrorHorizontalPanel.OuterTopControl,
                transform),
            MapSourcePoint(
                Mapper,
                kMirrorHorizontalPanel.ClosePoint,
                transform),
            kRoundedGeometryCurveSegments);
        StrokeCurrentPathRounded(
            DrawList,
            true,
            kGlyphColor,
            MapSourceStrokeWidth(
                Mapper,
                kMirrorHorizontalStrokeWidth,
                transform));
    }

    void DrawMirrorVerticalTrapezoid(
        ImDrawList* DrawList,
        const FSourceMapper& Mapper,
        float ReflectionScaleY)
    {
        const FSourceTransform transform = {
            1.0f,
            ReflectionScaleY,
            kMirrorVerticalOrigin.X,
            kMirrorVerticalOrigin.Y
        };

        DrawList->PathClear();
        DrawList->PathLineTo(
            MapSourcePoint(Mapper, kMirrorVerticalTrapezoid.Start, transform));
        DrawList->PathLineTo(
            MapSourcePoint(Mapper, kMirrorVerticalTrapezoid.TopRight, transform));
        DrawList->PathBezierQuadraticCurveTo(
            MapSourcePoint(
                Mapper,
                kMirrorVerticalTrapezoid.RightOuterControl,
                transform),
            MapSourcePoint(
                Mapper,
                kMirrorVerticalTrapezoid.RightOuter,
                transform),
            kRoundedGeometryCurveSegments);
        DrawList->PathLineTo(
            MapSourcePoint(
                Mapper,
                kMirrorVerticalTrapezoid.RightInner,
                transform));
        DrawList->PathBezierQuadraticCurveTo(
            MapSourcePoint(
                Mapper,
                kMirrorVerticalTrapezoid.RightInnerControl,
                transform),
            MapSourcePoint(
                Mapper,
                kMirrorVerticalTrapezoid.BottomRight,
                transform),
            kRoundedGeometryCurveSegments);
        DrawList->PathLineTo(
            MapSourcePoint(
                Mapper,
                kMirrorVerticalTrapezoid.BottomLeft,
                transform));
        DrawList->PathBezierQuadraticCurveTo(
            MapSourcePoint(
                Mapper,
                kMirrorVerticalTrapezoid.LeftInnerControl,
                transform),
            MapSourcePoint(
                Mapper,
                kMirrorVerticalTrapezoid.LeftInner,
                transform),
            kRoundedGeometryCurveSegments);
        DrawList->PathLineTo(
            MapSourcePoint(
                Mapper,
                kMirrorVerticalTrapezoid.LeftOuter,
                transform));
        DrawList->PathBezierQuadraticCurveTo(
            MapSourcePoint(
                Mapper,
                kMirrorVerticalTrapezoid.LeftOuterControl,
                transform),
            MapSourcePoint(
                Mapper,
                kMirrorVerticalTrapezoid.ClosePoint,
                transform),
            kRoundedGeometryCurveSegments);
        StrokeCurrentPathRounded(
            DrawList,
            true,
            kGlyphColor,
            MapSourceStrokeWidth(
                Mapper,
                kMirrorVerticalStrokeWidth,
                transform));
    }

    void DrawMirrorHorizontalGlyph(
        ImDrawList* DrawList,
        const ImVec2& Pos,
        float Size)
    {
        const FSourceMapper mapper =
            CreateSourceMapper(Pos, Size, kMirrorHorizontalViewBox);
        const FSourceTransform originTransform = {
            1.0f,
            1.0f,
            kMirrorHorizontalOrigin.X,
            kMirrorHorizontalOrigin.Y
        };

        DrawSourceRoundedLine(
            DrawList,
            mapper,
            kMirrorHorizontalAxisStart,
            kMirrorHorizontalAxisEnd,
            kMirrorHorizontalStrokeWidth,
            originTransform);
        DrawMirrorHorizontalPanel(DrawList, mapper, 1.0f);
        DrawMirrorHorizontalPanel(DrawList, mapper, -1.0f);
    }

    void DrawMirrorVerticalGlyph(
        ImDrawList* DrawList,
        const ImVec2& Pos,
        float Size)
    {
        const FSourceMapper mapper =
            CreateSourceMapper(Pos, Size, kMirrorVerticalViewBox);
        const FSourceTransform originTransform = {
            1.0f,
            1.0f,
            kMirrorVerticalOrigin.X,
            kMirrorVerticalOrigin.Y
        };

        DrawSourceRoundedLine(
            DrawList,
            mapper,
            kMirrorVerticalAxisStart,
            kMirrorVerticalAxisEnd,
            kMirrorVerticalAxisStrokeWidth,
            originTransform);
        DrawMirrorVerticalTrapezoid(DrawList, mapper, 1.0f);
        DrawMirrorVerticalTrapezoid(DrawList, mapper, -1.0f);
    }

    float DegreesToRadians(float Degrees)
    {
        return Degrees * kPi / kHalfTurnDegrees;
    }

    float GetNormalizedStrokeWidth(float Size, float StrokeRatio)
    {
        return std::max(
            Size * StrokeRatio,
            kMinimumAntiAliasedStrokePixels);
    }

    ImVec2 MapNormalizedPoint(
        const ImVec2& Pos,
        float Size,
        const FNormalizedPoint& Point)
    {
        return ImVec2(
            Pos.x + Point.X * Size,
            Pos.y + Point.Y * Size);
    }

    FNormalizedPoint TransformRotationPoint(
        const FNormalizedPoint& Point,
        ERotationDirection Direction)
    {
        if (Direction == ERotationDirection::CounterClockwise)
        {
            return { 1.0f - Point.X, Point.Y };
        }

        return Point;
    }

    /**
     * 顺时针版本是唯一的旋转组件；逆时针版本把整条圆弧和箭头沿中心轴镜像。
     * 圆弧只做一次 PathStroke，避免旧实现逐段补圆导致的小尺寸鼓点与毛刺。
     */
    void DrawRotationGlyph(
        ImDrawList* DrawList,
        const ImVec2& Pos,
        float Size,
        ERotationDirection Direction,
        ImU32 Color = kGlyphColor)
    {
        const float thickness =
            GetNormalizedStrokeWidth(Size, kRotationStrokeRatio);
        const float capRadius = thickness * 0.5f;
        const ImVec2 center =
            MapNormalizedPoint(Pos, Size, kRotationCenter);

        DrawList->PathClear();
        DrawList->PathArcTo(
            center,
            Size * kRotationRadiusRatio,
            DegreesToRadians(kRotationArcStartDegrees),
            DegreesToRadians(kRotationArcEndDegrees),
            kRotationArcSegments);

        if (Direction == ERotationDirection::CounterClockwise)
        {
            const float reflectionAxisX =
                Pos.x + Size * kNormalizedCenter;

            for (int32_t pointIndex = 0;
                 pointIndex < DrawList->_Path.Size;
                 ++pointIndex)
            {
                ImVec2& point = DrawList->_Path[pointIndex];
                point.x = reflectionAxisX + (reflectionAxisX - point.x);
            }
        }

        if (DrawList->_Path.Size < 2)
        {
            DrawList->PathClear();
            return;
        }

        const ImVec2 arcStart = DrawList->_Path.front();
        const ImVec2 arcEnd = DrawList->_Path.back();
        DrawList->PathStroke(
            Color,
            ImDrawFlags_None,
            thickness);
        DrawList->AddCircleFilled(arcStart, capRadius, Color);
        DrawList->AddCircleFilled(arcEnd, capRadius, Color);

        const FNormalizedPoint arrowPointsNormalized[] = {
            {
                kRotationArrowTip.X - kRotationArrowArmRatio,
                kRotationArrowTip.Y
            },
            kRotationArrowTip,
            {
                kRotationArrowTip.X,
                kRotationArrowTip.Y - kRotationArrowArmRatio
            }
        };
        ImVec2 arrowPoints[IM_ARRAYSIZE(arrowPointsNormalized)];

        for (int32_t pointIndex = 0;
             pointIndex < IM_ARRAYSIZE(arrowPointsNormalized);
             ++pointIndex)
        {
            arrowPoints[pointIndex] = MapNormalizedPoint(
                Pos,
                Size,
                TransformRotationPoint(
                    arrowPointsNormalized[pointIndex],
                    Direction));
        }

        DrawRoundedPolyline(
            DrawList,
            arrowPoints,
            IM_ARRAYSIZE(arrowPoints),
            false,
            Color,
            thickness);
    }

    FNormalizedPoint TransformTilePoint(
        const FNormalizedPoint& Point,
        ETileOrientation Orientation)
    {
        if (Orientation == ETileOrientation::Vertical)
        {
            return { 1.0f - Point.Y, Point.X };
        }

        return Point;
    }

    void DrawTileFrame(
        ImDrawList* DrawList,
        const ImVec2& Pos,
        float Size,
        const FNormalizedPoint& CanonicalCenter,
        ETileOrientation Orientation,
        float Thickness)
    {
        const FNormalizedPoint center =
            TransformTilePoint(CanonicalCenter, Orientation);
        const bool bVertical = Orientation == ETileOrientation::Vertical;
        const float halfWidth = Size * (
            bVertical
                ? kTileFrameHalfLongRatio
                : kTileFrameHalfShortRatio);
        const float halfHeight = Size * (
            bVertical
                ? kTileFrameHalfShortRatio
                : kTileFrameHalfLongRatio);
        const ImVec2 mappedCenter =
            MapNormalizedPoint(Pos, Size, center);
        const ImVec2 min(
            mappedCenter.x - halfWidth,
            mappedCenter.y - halfHeight);
        const ImVec2 max(
            mappedCenter.x + halfWidth,
            mappedCenter.y + halfHeight);

        // 直接构造路径，避开 AddRect 对坐标施加的隐式半像素偏移。
        DrawList->PathClear();
        DrawList->PathRect(
            min,
            max,
            Size * kTileFrameCornerRatio,
            ImDrawFlags_RoundCornersAll);
        DrawList->PathStroke(
            kGlyphColor,
            ImDrawFlags_Closed,
            Thickness);
    }

    /**
     * 水平版本只维护一个圆角框组件并平移复用；垂直版本将整组几何旋转 90°。
     * 去掉太阳、山线和中间分隔线后，16px 下仍能保持两个清楚分离的轮廓。
     */
    void DrawTileGlyph(
        ImDrawList* DrawList,
        const ImVec2& Pos,
        float Size,
        ETileOrientation Orientation)
    {
        const float thickness =
            GetNormalizedStrokeWidth(Size, kTileStrokeRatio);
        const FNormalizedPoint secondFrameCenter = {
            kTileFirstFrameCenter.X + kTileFrameRepeatOffset.X,
            kTileFirstFrameCenter.Y + kTileFrameRepeatOffset.Y
        };

        DrawTileFrame(
            DrawList,
            Pos,
            Size,
            kTileFirstFrameCenter,
            Orientation,
            thickness);
        DrawTileFrame(
            DrawList,
            Pos,
            Size,
            secondFrameCenter,
            Orientation,
            thickness);
    }

    FNormalizedPoint TransformChainPoint(
        const FNormalizedPoint& Point,
        bool bOppositeLink)
    {
        return bOppositeLink
            ? FNormalizedPoint{ 1.0f - Point.X, 1.0f - Point.Y }
            : Point;
    }

    void DrawChainLinkHalf(
        ImDrawList* DrawList,
        const ImVec2& Pos,
        float Size,
        bool bOppositeLink,
        float Thickness)
    {
        auto mapPoint = [&](const FNormalizedPoint& Point)
        {
            return MapNormalizedPoint(
                Pos,
                Size,
                TransformChainPoint(Point, bOppositeLink));
        };

        DrawList->PathClear();
        DrawList->PathLineTo(mapPoint(kChainStart));
        DrawList->PathBezierCubicCurveTo(
            mapPoint(kChainInnerControl1),
            mapPoint(kChainInnerControl2),
            mapPoint(kChainInnerEnd),
            kRoundedGeometryCurveSegments);
        DrawList->PathLineTo(mapPoint(kChainOuterStart));
        DrawList->PathBezierCubicCurveTo(
            mapPoint(kChainOuterControl1),
            mapPoint(kChainOuterControl2),
            mapPoint(kChainOuterEnd),
            kRoundedGeometryCurveSegments);
        DrawList->PathBezierCubicCurveTo(
            mapPoint(kChainReturnControl1),
            mapPoint(kChainReturnControl2),
            mapPoint(kChainReturnEnd),
            kRoundedGeometryCurveSegments);
        DrawList->PathLineTo(mapPoint(kChainTail));
        StrokeCurrentPathRounded(
            DrawList,
            false,
            kGlyphColor,
            Thickness);
    }

    void DrawChainLinkGlyph(
        ImDrawList* DrawList,
        const ImVec2& Pos,
        float Size)
    {
        const float thickness =
            GetNormalizedStrokeWidth(Size, kChainStrokeRatio);

        DrawChainLinkHalf(DrawList, Pos, Size, false, thickness);
        DrawChainLinkHalf(DrawList, Pos, Size, true, thickness);
    }
}

void FUiIcons::DrawFolder(ImDrawList* DrawList, const ImVec2& Pos, float Size)
{
    if (!DrawList || Size <= 0.0f)
    {
        return;
    }

    const float width = Size;
    const float height = Size * 0.78f;
    const float top = Pos.y + (Size - height) * 0.5f;
    const float tabHeight = height * 0.22f;
    const float rounding = Size * 0.10f;

    // 背板：左上角的小凸起 + 整块底
    DrawList->AddRectFilled(
        ImVec2(Pos.x, top),
        ImVec2(Pos.x + width * 0.52f, top + tabHeight * 2.0f),
        kFolderBack, rounding, ImDrawFlags_RoundCornersTop);

    DrawList->AddRectFilled(
        ImVec2(Pos.x, top + tabHeight),
        ImVec2(Pos.x + width, top + height),
        kFolderBack, rounding);

    // 前板：整体下移一点，留出上沿的深色边
    DrawList->AddRectFilled(
        ImVec2(Pos.x, top + tabHeight * 1.7f),
        ImVec2(Pos.x + width, top + height),
        kFolderFront, rounding, ImDrawFlags_RoundCornersBottom);
}

void FUiIcons::DrawFile(ImDrawList* DrawList, const ImVec2& Pos, float Size)
{
    if (!DrawList || Size <= 0.0f)
    {
        return;
    }

    const float width = Size * 0.74f;
    const float height = Size * 0.92f;
    const float left = Pos.x + (Size - width) * 0.5f;
    const float top = Pos.y + (Size - height) * 0.5f;
    const float fold = Size * 0.30f;

    // 右上角切掉一块，形成折角纸张的轮廓（凸多边形，可以直接填充）
    const ImVec2 outline[5] = {
        ImVec2(left, top),
        ImVec2(left + width - fold, top),
        ImVec2(left + width, top + fold),
        ImVec2(left + width, top + height),
        ImVec2(left, top + height),
    };

    DrawList->AddConvexPolyFilled(outline, 5, kFileFill);

    // 折角本身
    DrawList->AddTriangleFilled(
        ImVec2(left + width - fold, top),
        ImVec2(left + width, top + fold),
        ImVec2(left + width - fold, top + fold),
        kFileFold);

    DrawList->AddPolyline(outline, 5, kFileStroke, ImDrawFlags_Closed, 1.0f);
}

bool FUiIcons::FolderButton(const char* Id)
{
    const float buttonSize = ImGui::GetFrameHeight();
    const ImVec2 buttonPos = ImGui::GetCursorScreenPos();

    const bool bClicked = ImGui::Button(Id, ImVec2(buttonSize, buttonSize));

    const float iconSize = buttonSize * 0.62f;
    const float inset = (buttonSize - iconSize) * 0.5f;

    DrawFolder(ImGui::GetWindowDrawList(), ImVec2(buttonPos.x + inset, buttonPos.y + inset), iconSize);

    return bClicked;
}

void FUiIcons::DrawViewerGlyph(ImDrawList* DrawList, EViewerGlyph Glyph, const ImVec2& Pos, float Size)
{
    if (!DrawList || Size <= 0.0f)
    {
        return;
    }

    switch (Glyph)
    {
    case EViewerGlyph::Exif:
    {
        const float inset = Size * kExifGlyphInsetRatio;
        const float stroke = GetNormalizedStrokeWidth(Size, kExifGlyphStrokeRatio);
        DrawList->AddRect(ImVec2(Pos.x + inset, Pos.y + inset),
            ImVec2(Pos.x + Size - inset, Pos.y + Size - inset), kGlyphColor,
            Size * kExifGlyphRoundingRatio, ImDrawFlags_None, stroke);
        const float centerX = Pos.x + Size * kExifGlyphCenterRatio;
        DrawList->AddCircleFilled(ImVec2(centerX, Pos.y + Size * kExifGlyphDotYRatio), stroke, kGlyphColor);
        DrawList->AddLine(ImVec2(centerX, Pos.y + Size * kExifGlyphStemTopRatio),
            ImVec2(centerX, Pos.y + Size * kExifGlyphStemBottomRatio), kGlyphColor, stroke);
        break;
    }
    case EViewerGlyph::MirrorHorizontal:
        DrawMirrorHorizontalGlyph(DrawList, Pos, Size);
        break;

    case EViewerGlyph::MirrorVertical:
        DrawMirrorVerticalGlyph(DrawList, Pos, Size);
        break;

    case EViewerGlyph::RotateClockwise:
        DrawRotationGlyph(
            DrawList,
            Pos,
            Size,
            ERotationDirection::Clockwise);
        break;

    case EViewerGlyph::RotateCounterClockwise:
        DrawRotationGlyph(
            DrawList,
            Pos,
            Size,
            ERotationDirection::CounterClockwise);
        break;

    case EViewerGlyph::ChainLink:
        DrawChainLinkGlyph(DrawList, Pos, Size);
        break;

    case EViewerGlyph::TileHorizontal:
        DrawTileGlyph(
            DrawList,
            Pos,
            Size,
            ETileOrientation::Horizontal);
        break;

    case EViewerGlyph::TileVertical:
        DrawTileGlyph(
            DrawList,
            Pos,
            Size,
            ETileOrientation::Vertical);
        break;

    case EViewerGlyph::Close:
    {
        const float inset = Size * kCloseGlyphInsetRatio;
        const ImVec2 min(Pos.x + inset, Pos.y + inset);
        const ImVec2 max(Pos.x + Size - inset, Pos.y + Size - inset);
        const float stroke = GetNormalizedStrokeWidth(Size, kCloseGlyphStrokeRatio);
        DrawList->AddLine(min, max, kGlyphColor, stroke);
        DrawList->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, min.y), kGlyphColor, stroke);
        break;
    }
    }
}

ImU32 FUiIcons::GetViewerGlyphColor()
{
    return kGlyphColor;
}

bool FUiIcons::ResetButton(const char* Id, const char* Tooltip, float Size)
{
    const float buttonSize = Size > 0.0f ? Size : ImGui::GetFrameHeight();
    const ImVec2 buttonPos = ImGui::GetCursorScreenPos();
    const bool bClicked = ImGui::Button(Id, ImVec2(buttonSize, buttonSize));
    const float iconSize = buttonSize * kViewerButtonIconRatio;
    const float inset = (buttonSize - iconSize) * 0.5f;
    // 复用旋转图标几何；设置控件跟随文字色和禁用透明度，避免主题预览时失去对比。
    DrawRotationGlyph(ImGui::GetWindowDrawList(),
        ImVec2(buttonPos.x + inset, buttonPos.y + inset), iconSize,
        ERotationDirection::CounterClockwise, ImGui::GetColorU32(ImGuiCol_Text));
    if (Tooltip && *Tooltip && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", Tooltip);
    }
    return bClicked;
}

bool FUiIcons::ViewerButton(
    const char* Id,
    EViewerGlyph Glyph,
    const char* Tooltip,
    bool bSelected,
    EViewerButtonStyle Style)
{
    const float buttonSize = ImGui::GetFrameHeight();
    const ImVec2 buttonPos = ImGui::GetCursorScreenPos();

    const bool bOverlay = (Style == EViewerButtonStyle::Overlay);
    int32_t colorCount = 0;
    int32_t styleCount = 0;

    if (bOverlay)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(1.0f, 1.0f, 1.0f, kOverlayButtonHoveredAlpha));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImVec4(1.0f, 1.0f, 1.0f, kOverlayButtonActiveAlpha));
        colorCount = kOverlayButtonColorStackCount;
    }
    else if (bSelected)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            FUiTheme::GetImGuiColor(
                FUserSettings::EThemeColorRole::Accent));
        ImGui::PushStyleColor(
            ImGuiCol_Border,
            ImVec4(1.0f, 1.0f, 1.0f, kSelectedButtonBorderAlpha));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        colorCount = 2;
        styleCount = 1;
    }

    const bool bClicked = ImGui::Button(Id, ImVec2(buttonSize, buttonSize));

    if (styleCount > 0)
    {
        ImGui::PopStyleVar(styleCount);
    }

    if (colorCount > 0)
    {
        ImGui::PopStyleColor(colorCount);
    }

    const float iconSize = buttonSize * kViewerButtonIconRatio;
    const float inset = (buttonSize - iconSize) * 0.5f;

    DrawViewerGlyph(ImGui::GetWindowDrawList(), Glyph,
                    ImVec2(buttonPos.x + inset, buttonPos.y + inset), iconSize);

    // 图标按钮没有文字，光看形状不一定认得出，提示是必须的
    if (Tooltip && *Tooltip && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", Tooltip);
    }

    return bClicked;
}

bool FUiIcons::IconSelectable(
    const char* Id,
    const char* Label,
    bool bSelected,
    bool bIsFolder,
    bool bCompared,
    bool bActivateOnNavigation)
{
    const float lineHeight = ImGui::GetTextLineHeight();
    const float iconSize = lineHeight * 0.92f;
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    const bool bHighlighted = bSelected || bCompared;

    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    ImVec4 fileSelected;

    // Selectable 只提供命中区域，图标和文字随后手动画：
    // 这样两者的对齐不受字体字形宽度影响，也不用靠空格凑缩进
    if (bHighlighted)
    {
        fileSelected = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
        fileSelected.w = kFileSelectedAlpha;
        ImVec4 fileSelectedHovered =
            ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
        fileSelectedHovered.w = kFileSelectedHoveredAlpha;
        ImVec4 fileSelectedActive =
            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        fileSelectedActive.w = kFileSelectedActiveAlpha;

        ImGui::PushStyleColor(
            ImGuiCol_Header,
            bCompared ? kFileCompared : fileSelected);
        ImGui::PushStyleColor(
            ImGuiCol_HeaderHovered,
            bCompared ? kFileComparedHovered : fileSelectedHovered);
        ImGui::PushStyleColor(
            ImGuiCol_HeaderActive,
            bCompared ? kFileComparedActive : fileSelectedActive);
    }

    // ImGui 默认只移动导航焦点，不激活 Selectable。文件浏览器需要方向键落到
    // 新文件时立即走与鼠标点击相同的打开链路，因此仅由调用方按需启用内部导航标志。
    const ImGuiSelectableFlags selectableFlags = bActivateOnNavigation
        ? ImGuiSelectableFlags_SelectOnNav
        : ImGuiSelectableFlags_None;
    const bool bActivated = ImGui::Selectable(Id, bHighlighted, selectableFlags);

    if (bHighlighted)
    {
        ImGui::PopStyleColor(3);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 rowMax = ImGui::GetItemRectMax();

    // 文字自己画就没有 Selectable 的自动裁剪了，超长文件名会溢出到面板外，
    // 因此显式把这一行的绘制夹在行矩形内
    drawList->PushClipRect(rowMin, rowMax, true);

    if (bSelected && bCompared)
    {
        drawList->AddRectFilled(
            rowMin,
            ImVec2(
                rowMin.x + FUiScale::Apply(kDualFileHighlightAccentWidth),
                rowMax.y),
            ImGui::ColorConvertFloat4ToU32(fileSelected));
    }

    const ImVec2 iconPos(rowMin.x + gap, rowMin.y + (lineHeight - iconSize) * 0.5f);

    if (bIsFolder)
    {
        DrawFolder(drawList, iconPos, iconSize);
    }
    else
    {
        DrawFile(drawList, iconPos, iconSize);
    }

    drawList->AddText(
        ImVec2(rowMin.x + gap + iconSize + gap, rowMin.y),
        bHighlighted ? kGlyphColor : ImGui::GetColorU32(ImGuiCol_Text),
        Label);

    drawList->PopClipRect();

    return bActivated;
}

void FUiIcons::DrawSpinner(ImDrawList* DrawList, const ImVec2& Center, float Radius, float Thickness, double Time)
{
    if (!DrawList || Radius <= 0.0f)
    {
        return;
    }

    const ImU32 spinnerTrack = ImGui::ColorConvertFloat4ToU32(
        FUiTheme::GetImGuiColor(
            FUserSettings::EThemeColorRole::Control,
            kSpinnerTrackAlpha));
    const ImU32 spinnerArc = ImGui::ColorConvertFloat4ToU32(
        FUiTheme::GetImGuiColor(
            FUserSettings::EThemeColorRole::Control));

    // 先画整圈的浅色轨道：缺口才读得出是"跑过去了"，而不是"画漏了一段"
    DrawList->PathClear();
    DrawList->PathArcTo(Center, Radius, 0.0f, kPi * 2.0f, kSpinnerSegments);
    DrawList->PathStroke(spinnerTrack, ImDrawFlags_None, Thickness);

    // 起点匀速转，弧长再做一次呼吸 —— 比单纯匀速转的等长圆弧更容易看出"还在动"
    const float t = static_cast<float>(Time);
    const float start = t * 3.2f;
    const float sweep = kPi * (0.75f + 0.45f * std::sin(t * 2.1f));

    DrawList->PathClear();
    DrawList->PathArcTo(Center, Radius, start, start + sweep, kSpinnerSegments);
    DrawList->PathStroke(spinnerArc, ImDrawFlags_None, Thickness);
}

void FUiIcons::DrawBusyOverlay(const char* Label, const char* Detail)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    if (!viewport)
    {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);

    if (!drawList)
    {
        return;
    }

    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;

    drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), kScrim);

    const float fontSize = ImGui::GetFontSize();
    const float radius = fontSize * 1.15f;
    const float padding = fontSize * 1.5f;
    const float gap = fontSize * 0.75f;

    const bool bHasLabel = Label && *Label;
    const bool bHasDetail = Detail && *Detail;

    const ImVec2 labelSize = bHasLabel ? ImGui::CalcTextSize(Label) : ImVec2(0.0f, 0.0f);
    const ImVec2 detailSize = bHasDetail ? ImGui::CalcTextSize(Detail) : ImVec2(0.0f, 0.0f);

    float contentHeight = radius * 2.0f;
    float contentWidth = radius * 2.0f;

    if (bHasLabel)
    {
        contentHeight += gap + labelSize.y;
        contentWidth = std::max(contentWidth, labelSize.x);
    }

    if (bHasDetail)
    {
        contentHeight += gap * 0.5f + detailSize.y;
        contentWidth = std::max(contentWidth, detailSize.x);
    }

    // 卡片给个下限宽度，否则文字短的时候会缩成一个窄条
    contentWidth = std::max(contentWidth, fontSize * 9.0f);

    const ImVec2 center(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
    const ImVec2 cardHalf((contentWidth + padding * 2.0f) * 0.5f, (contentHeight + padding * 2.0f) * 0.5f);
    const ImVec2 cardMin(center.x - cardHalf.x, center.y - cardHalf.y);
    const ImVec2 cardMax(center.x + cardHalf.x, center.y + cardHalf.y);

    const float rounding = fontSize * 0.5f;

    drawList->AddRectFilled(cardMin, cardMax, kCardFill, rounding);
    drawList->AddRect(cardMin, cardMax, kCardBorder, rounding);

    float y = cardMin.y + padding;

    DrawSpinner(drawList, ImVec2(center.x, y + radius), radius, std::max(2.0f, radius * 0.24f), ImGui::GetTime());
    y += radius * 2.0f;

    if (bHasLabel)
    {
        y += gap;
        drawList->AddText(ImVec2(center.x - labelSize.x * 0.5f, y), kLabelText, Label);
        y += labelSize.y;
    }

    if (bHasDetail)
    {
        y += gap * 0.5f;
        drawList->AddText(ImVec2(center.x - detailSize.x * 0.5f, y), kDetailText, Detail);
    }
}
