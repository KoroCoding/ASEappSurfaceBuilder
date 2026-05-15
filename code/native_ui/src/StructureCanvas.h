#pragma once

#include <QRect>
#include <QHash>
#include <QWidget>
#include <vector>

#include "ElementStyle.h"
#include "StructureData.h"

class StructureCanvas : public QWidget {
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
        bool perspective = false;
        bool depthCue = false;
        double atomScale = 1.0;
        QHash<QString, BondDistanceRange> customBondRanges;
    };

    explicit StructureCanvas(QWidget* parent = nullptr);

    void setStructure(const StructureData& structure);
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
    void emptyCanvasActivated();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
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

    QVector3D rotatePoint(const QVector3D& point) const;
    QPointF projectPoint(const QVector3D& point, const QRectF& rect, double scale) const;
    std::vector<BondSegment> buildBondPairs() const;
    QColor backgroundColor() const;
    int pickAtomAt(const QPoint& pos) const;
    std::vector<int> pickAtomsAt(const QPoint& pos) const;
    int pickNextCtrlAtomAt(const QPoint& pos) const;
    QVector3D sceneCenter() const;
    double sceneScale(const QRectF& viewport, const QVector3D& center) const;
    void rebuildSceneCache();
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

    StructureData m_structure;
    std::vector<NativeAtom> m_previewAtoms;
    DisplayOptions m_displayOptions;
    InteractionMode m_interactionMode = InteractionMode::View;
    std::vector<BondSegment> m_cachedBonds;
    std::vector<AtomImage> m_cachedAtomImages;
    QVector3D m_cachedCenter;
    double m_cachedRadius = 1.0;
    double m_cachedNearestAtomDistance = 0.0;
    double m_cachedMaxAtomRadius = 1.0;
    int m_focusAtomId = -1;
    std::vector<int> m_selectedAtomIds;
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
};
