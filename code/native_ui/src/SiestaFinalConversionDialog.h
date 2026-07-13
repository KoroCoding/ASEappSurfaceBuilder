#pragma once

#include "SiestaFinalConverter.h"

#include <QDialog>

class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

class SiestaFinalConversionDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SiestaFinalConversionDialog(const QString& initialDirectory,
                                         bool japanese,
                                         QWidget* parent = nullptr);

    QString outputPath() const { return m_writtenOutputPath; }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildUi();
    void chooseInput();
    void chooseCell();
    void chooseOutput();
    void setInputPath(const QString& path);
    void startAnalysis();
    void finishAnalysis(int generation, SiestaFinalAnalysis analysis);
    void updatePreview();
    void setBusy(bool busy);
    void convertAndAccept();
    void showStatus(const QString& text, bool error = false);

    QString m_initialDirectory;
    bool m_japanese = true;
    int m_analysisGeneration = 0;
    SiestaFinalAnalysis m_analysis;
    QString m_writtenOutputPath;
    bool m_outputEditedByUser = false;

    QLineEdit* m_inputEdit = nullptr;
    QLineEdit* m_cellEdit = nullptr;
    QLineEdit* m_outputEdit = nullptr;
    QComboBox* m_coordinateCombo = nullptr;
    QLabel* m_atomValue = nullptr;
    QLabel* m_elementValue = nullptr;
    QLabel* m_fixedValue = nullptr;
    QLabel* m_cellValue = nullptr;
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_inputButton = nullptr;
    QPushButton* m_cellButton = nullptr;
    QPushButton* m_outputButton = nullptr;
    QPushButton* m_convertButton = nullptr;
};
