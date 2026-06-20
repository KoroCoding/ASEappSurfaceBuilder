#pragma once

#include <QHash>
#include <QImage>
#include <QOpenGLExtraFunctions>
#include <QOpenGLWidget>
#include <QRect>
#include <memory>
#include <vector>

#include "ElementStyle.h"
#include "StructureData.h"

class QKeyEvent;

class StructureCanvas : public QOpenGLWidget, protected QOpenGLExtraFunctions {
    Q_OBJECT
public:
    enum class InteractionMode {
        View,
        MoveAtoms,
        MoveModel
    };

    struct DisplayOptions {
        bool showCell = true;
        bool showBonds = true;
        bool showOutsideCell = true;
        bool showAxes = true;
        bool showLabels = false;
        // Keep fixed-axis constraints visible without changing atom geometry.
        bool highlightFixedAtoms = true;
        bool perspective = false;
        bool depthCue = false;
        double atomScale = 1.0;
        QHash<QString, BondDistanceRange> customBondRanges;
    };

    explicit StructureCanvas(QWidget* parent = nullptr);
    ~StructureCanvas() override;

    void setStructure(const StructureData& structure);
    void updateStructureCoordinates(const StructureData& structure);
    void setSelectedAtomIds(const std::vector<int>& atomIds);
    void setPreviewAtoms(const std::vector<NativeAtom>& atoms);
    void resetView();
    void fitToStructure();
    void focusAtom(int atomId);
    void rotateBy(double yawDelta, double pitchDelta);
    void panBy(double dx, double dy);
    void zoomBy(double factor);
    void setViewDirection(const QVector3D& direction, bool resetPan = true);
    void setViewDirection(const QVector3D& direction, const QVector3D& upHint, bool resetPan);
    void setAxisAlignedView(const QVector3D& horizontalAxis, const QVector3D& upHint, bool resetPan = true);
    void setJapanese(bool japanese);
    void setInteractionMode(InteractionMode mode);
    InteractionMode interactionMode() const;
    QVector3D viewForward() const;
    QSize minimumSizeHint() const override;
    void setDisplayOptions(const DisplayOptions& options);
    DisplayOptions displayOptions() const;
    int bondCount() const;

signals:
    void atomActivated(int atomId);
    void atomPrimarySelected(int atomId);
    void selectedAtomsTranslated(const QVector3D& delta);
    void selectedAtomsTranslationFinished();
    void clearSelectionRequested();
    void emptyCanvasActivated();
    void frameRendered(double cpuFrameMs, int atomInstances, int lineInstances);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct BondSegment {
        int atomA = -1;
        int atomB = -1;
        int imageA = 0;
        int imageB = 0;
        int imageC = 0;
        QVector3D shiftB;
        double distance = 0.0;
    };

    struct AtomImage {
        int atom = -1;
        int imageA = 0;
        int imageB = 0;
        int imageC = 0;
        QVector3D shift;
    };

    struct PickIndexEntry {
        int atomId = -1;
        QPointF point;
        double radius = 0.0;
        double depth = 0.0;
    };

    struct AtomRenderInstance {
        float center[3] = {};
        float radius = 1.0f;
        float color[4] = {};
        float pickColor[3] = {};
        float padding = 0.0f;
    };

    struct LineRenderInstance {
        float start[3] = {};
        float end[3] = {};
        float startRadius = 0.0f;
        float endRadius = 0.0f;
        float halfWidth = 0.0f;
        float depthBias = 0.0f;
        float color[4] = {};
        float dash = 0.0f;
        float padding[3] = {};
    };

    struct LabelSource {
        float center[3] = {};
        QString text;
    };

    struct LabelRenderInstance {
        float center[3] = {};
        float depthBias = 0.0f;
        float size[2] = {};
        float uvMin[2] = {};
        float uvMax[2] = {};
    };

    struct GpuViewState {
        QSizeF canvasSize;
        QPointF viewportCenter;
        QVector3D sceneCenter;
        QVector3D viewRight;
        QVector3D viewUp;
        QVector3D viewForward;
        QPointF panOffset;
        float scale = 1.0f;
        float atomScale = 1.0f;
        float densityScale = 1.0f;
        float depthMax = 1.0f;
        float invDepthRange = 1.0f;
        bool perspective = false;
        bool depthCue = false;
    };

    struct OpenGLResources;

    QVector3D rotatePoint(const QVector3D& point) const;
    QPointF projectPoint(const QVector3D& point, const QRectF& rect, double scale) const;
    std::vector<BondSegment> buildBondPairs() const;
    QColor backgroundColor() const;
    int pickAtomAt(const QPoint& pos);
    int pickAtomAtGpu(const QPoint& pos);
    std::vector<int> pickAtomsAt(const QPoint& pos);
    std::vector<int> pickAtomsAtCpu(const QPoint& pos);
    int pickNextCtrlAtomAt(const QPoint& pos);
    void invalidatePickIndex() const;
    void rebuildPickIndex() const;
    std::vector<int> pickAtomsInScreenRect(const QRectF& selection);
    QVector3D sceneCenter() const;
    double sceneScale(const QRectF& viewport, const QVector3D& center) const;
    void rebuildSceneCache(bool rebuildBonds = true);
    void rebuildRenderInstances();
    void rebuildPreviewRenderInstances();
    void rebuildLabelAtlas(int maxTextureSize);
    void markStaticRenderInstancesDirty();
    void markPreviewRenderInstancesDirty();
    GpuViewState currentGpuViewState() const;
    double depthPerspective(double z) const;
    void setBasisFromView(const QVector3D& forward, const QVector3D& upHint);
    void rotateBasisFromDrag(const QPoint& delta);
    void addAtomsInCtrlSelectionRect();
    void addAtomsAt(const QPoint& pos);
    bool isAtomSelected(int atomId) const;
    QRect ctrlSelectionRect() const;
    QRect visibleCtrlSelectionRect() const;
    void updateInteractionCursor();
    void zoomAt(double factor, const QPointF& position);
    void releaseOpenGLResources();

    StructureData m_structure;
    std::vector<NativeAtom> m_previewAtoms;
    DisplayOptions m_displayOptions;
    InteractionMode m_interactionMode = InteractionMode::View;
    std::vector<BondSegment> m_cachedBonds;
    std::vector<AtomImage> m_cachedAtomImages;
    std::vector<AtomRenderInstance> m_atomRenderInstances;
    std::vector<LineRenderInstance> m_lineRenderInstances;
    std::vector<AtomRenderInstance> m_previewAtomRenderInstances;
    std::vector<LineRenderInstance> m_previewLineRenderInstances;
    std::vector<LabelSource> m_labelSources;
    std::vector<LabelRenderInstance> m_labelRenderInstances;
    std::vector<int> m_labelRenderSourceIndices;
    std::vector<int> m_atomRenderInstanceImageIndices;
    QHash<int, std::vector<int>> m_atomIdToRenderInstanceIndices;
    std::vector<int> m_cachedBondLineStartIndices;
    std::vector<int> m_cachedBondLineCounts;
    QHash<int, std::vector<int>> m_atomIdToCachedBondIndices;
    std::vector<std::vector<int>> m_labelSourceToRenderInstanceIndices;
    std::vector<int> m_dirtyAtomRenderInstanceIndices;
    std::vector<int> m_dirtyLineRenderInstanceIndices;
    std::vector<int> m_dirtyLabelRenderInstanceIndices;
    QImage m_labelAtlasImage;
    mutable std::vector<PickIndexEntry> m_pickIndexEntries;
    mutable QHash<qint64, std::vector<int>> m_pickIndexGrid;
    mutable bool m_pickIndexDirty = true;
    mutable QSize m_pickIndexCanvasSize;
    bool m_atomRenderInstancesDirty = true;
    bool m_lineRenderInstancesDirty = true;
    bool m_previewAtomRenderInstancesDirty = true;
    bool m_previewLineRenderInstancesDirty = true;
    bool m_labelRenderInstancesDirty = true;
    bool m_labelAtlasDirty = true;
    bool m_labelTextureDirty = true;
    QVector3D m_cachedCenter;
    double m_cachedRadius = 1.0;
    double m_cachedNearestAtomDistance = 0.0;
    double m_cachedMaxAtomRadius = 1.0;
    int m_focusAtomId = -1;
    std::vector<int> m_selectedAtomIds;
    QHash<int, int> m_selectedAtomOrder;
    QVector3D m_viewRight = QVector3D(1.0f, 0.0f, 0.0f);
    QVector3D m_viewUp = QVector3D(0.0f, 1.0f, 0.0f);
    QVector3D m_viewForward = QVector3D(0.0f, 0.0f, 1.0f);
    double m_zoom = 1.0;
    QPointF m_panOffset;
    QPoint m_mousePressPos;
    QPoint m_lastMousePos;
    bool m_dragged = false;
    bool m_draggingSelection = false;
    bool m_ctrlSelectingAtoms = false;
    bool m_japanese = true;
    Qt::MouseButton m_activeButton = Qt::NoButton;
    QPoint m_ctrlSelectionStart;
    QPoint m_ctrlSelectionEnd;
    int m_ctrlPressAtomId = -1;
    std::unique_ptr<OpenGLResources> m_openGL;
};
