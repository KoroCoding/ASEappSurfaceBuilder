#include "SiestaFinalConversionDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QPalette>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

SiestaFinalConversionDialog::SiestaFinalConversionDialog(const QString& initialDirectory,
                                                         bool japanese,
                                                         QWidget* parent)
    : QDialog(parent), m_initialDirectory(initialDirectory), m_japanese(japanese) {
    buildUi();
}

void SiestaFinalConversionDialog::buildUi() {
    setWindowTitle(m_japanese ? QStringLiteral("SIESTA最終構造 → VASP") : QStringLiteral("SIESTA final structure → VASP"));
    setAcceptDrops(true);
    setMinimumWidth(680);
    resize(760, 470);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(14);
    auto* title = new QLabel(m_japanese
        ? QStringLiteral("対象・格子・保存先を確認してから変換します")
        : QStringLiteral("Review the source, cell, and destination before conversion"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    title->setFont(titleFont);
    root->addWidget(title);
    auto* hint = new QLabel(m_japanese
        ? QStringLiteral("SIESTA出力をこの画面へドロップすることもできます。セルは同じフォルダーから自動検出します。")
        : QStringLiteral("You can drop a SIESTA output here. A cell file is detected from the same folder."), this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto* formGroup = new QGroupBox(m_japanese ? QStringLiteral("変換設定") : QStringLiteral("Conversion settings"), this);
    auto* form = new QGridLayout(formGroup);
    form->setColumnStretch(1, 1);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(10);
    m_inputEdit = new QLineEdit(formGroup);
    m_inputEdit->setReadOnly(true);
    m_inputEdit->setPlaceholderText(m_japanese ? QStringLiteral("SIESTA .out / STRUCT_OUT / XV") : QStringLiteral("SIESTA .out / STRUCT_OUT / XV"));
    m_inputButton = new QPushButton(m_japanese ? QStringLiteral("参照…") : QStringLiteral("Browse…"), formGroup);
    m_cellEdit = new QLineEdit(formGroup);
    m_cellEdit->setReadOnly(true);
    m_cellEdit->setPlaceholderText(m_japanese ? QStringLiteral("入力解析後に自動検出") : QStringLiteral("Auto-detected after analysis"));
    m_cellButton = new QPushButton(m_japanese ? QStringLiteral("変更…") : QStringLiteral("Change…"), formGroup);
    m_cellButton->setEnabled(false);
    m_outputEdit = new QLineEdit(formGroup);
    m_outputButton = new QPushButton(m_japanese ? QStringLiteral("参照…") : QStringLiteral("Browse…"), formGroup);
    m_coordinateCombo = new QComboBox(formGroup);
    m_coordinateCombo->addItem(QStringLiteral("Cartesian / Å"), static_cast<int>(StructureCoordinateMode::Cartesian));
    m_coordinateCombo->addItem(m_japanese ? QStringLiteral("Direct / 分率座標") : QStringLiteral("Direct / fractional"), static_cast<int>(StructureCoordinateMode::Direct));
    form->addWidget(new QLabel(m_japanese ? QStringLiteral("SIESTA出力") : QStringLiteral("SIESTA output"), formGroup), 0, 0);
    form->addWidget(m_inputEdit, 0, 1);
    form->addWidget(m_inputButton, 0, 2);
    form->addWidget(new QLabel(m_japanese ? QStringLiteral("格子情報") : QStringLiteral("Cell source"), formGroup), 1, 0);
    form->addWidget(m_cellEdit, 1, 1);
    form->addWidget(m_cellButton, 1, 2);
    form->addWidget(new QLabel(m_japanese ? QStringLiteral("保存先") : QStringLiteral("Destination"), formGroup), 2, 0);
    form->addWidget(m_outputEdit, 2, 1);
    form->addWidget(m_outputButton, 2, 2);
    form->addWidget(new QLabel(m_japanese ? QStringLiteral("座標形式") : QStringLiteral("Coordinates"), formGroup), 3, 0);
    form->addWidget(m_coordinateCombo, 3, 1);
    root->addWidget(formGroup);

    auto* summaryGroup = new QGroupBox(m_japanese ? QStringLiteral("変換プレビュー") : QStringLiteral("Conversion preview"), this);
    auto* summary = new QGridLayout(summaryGroup);
    const QString dash = QStringLiteral("—");
    m_atomValue = new QLabel(dash, summaryGroup);
    m_elementValue = new QLabel(dash, summaryGroup);
    m_fixedValue = new QLabel(dash, summaryGroup);
    m_cellValue = new QLabel(dash, summaryGroup);
    m_elementValue->setWordWrap(true);
    m_cellValue->setWordWrap(true);
    summary->addWidget(new QLabel(m_japanese ? QStringLiteral("原子数") : QStringLiteral("Atoms"), summaryGroup), 0, 0);
    summary->addWidget(m_atomValue, 0, 1);
    summary->addWidget(new QLabel(m_japanese ? QStringLiteral("拘束原子") : QStringLiteral("Constrained"), summaryGroup), 0, 2);
    summary->addWidget(m_fixedValue, 0, 3);
    summary->addWidget(new QLabel(m_japanese ? QStringLiteral("元素") : QStringLiteral("Elements"), summaryGroup), 1, 0);
    summary->addWidget(m_elementValue, 1, 1, 1, 3);
    summary->addWidget(new QLabel(m_japanese ? QStringLiteral("セル長 (Å)") : QStringLiteral("Cell lengths (Å)"), summaryGroup), 2, 0);
    summary->addWidget(m_cellValue, 2, 1, 1, 3);
    summary->setColumnStretch(1, 1);
    summary->setColumnStretch(3, 1);
    root->addWidget(summaryGroup);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->hide();
    root->addWidget(m_progress);
    m_statusLabel = new QLabel(m_japanese ? QStringLiteral("SIESTA出力を選択してください。") : QStringLiteral("Select a SIESTA output."), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_statusLabel->setMinimumHeight(34);
    root->addWidget(m_statusLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_convertButton = buttons->addButton(m_japanese ? QStringLiteral("VASPに変換して開く") : QStringLiteral("Convert and open"), QDialogButtonBox::AcceptRole);
    m_convertButton->setDefault(true);
    m_convertButton->setEnabled(false);
    root->addWidget(buttons);

    connect(m_inputButton, &QPushButton::clicked, this, &SiestaFinalConversionDialog::chooseInput);
    connect(m_cellButton, &QPushButton::clicked, this, &SiestaFinalConversionDialog::chooseCell);
    connect(m_outputButton, &QPushButton::clicked, this, &SiestaFinalConversionDialog::chooseOutput);
    connect(m_outputEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(m_outputEdit, &QLineEdit::textEdited, this, [this]() { m_outputEditedByUser = true; });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_convertButton, &QPushButton::clicked, this, &SiestaFinalConversionDialog::convertAndAccept);
}

void SiestaFinalConversionDialog::chooseInput() {
    const QString path = QFileDialog::getOpenFileName(this,
        m_japanese ? QStringLiteral("SIESTA最終構造を選択") : QStringLiteral("Select SIESTA final structure"),
        m_initialDirectory,
        QStringLiteral("SIESTA output (*.out *.txt *.STRUCT_OUT *.XV);;All files (*.*)"));
    if (!path.isEmpty()) setInputPath(path);
}

void SiestaFinalConversionDialog::setInputPath(const QString& path) {
    if (!QFileInfo(path).isFile()) return;
    m_inputEdit->setText(QFileInfo(path).absoluteFilePath());
    m_outputEditedByUser = false;
    m_outputEdit->setText(SiestaFinalConverter::suggestedOutputPath(path));
    m_initialDirectory = QFileInfo(path).absolutePath();
    startAnalysis();
}

void SiestaFinalConversionDialog::startAnalysis() {
    const QString sourcePath = m_inputEdit->text();
    if (sourcePath.isEmpty()) return;
    const int generation = ++m_analysisGeneration;
    m_analysis = {};
    setBusy(true);
    showStatus(m_japanese ? QStringLiteral("最終構造と格子情報を解析しています…") : QStringLiteral("Analyzing final geometry and cell…"));
    QPointer<SiestaFinalConversionDialog> self(this);
    QThread* worker = QThread::create([self, sourcePath, generation]() {
        const SiestaFinalAnalysis analysis = SiestaFinalConverter().analyze(sourcePath);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, generation, analysis]() {
            if (self) self->finishAnalysis(generation, analysis);
        }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void SiestaFinalConversionDialog::finishAnalysis(int generation, SiestaFinalAnalysis analysis) {
    if (generation != m_analysisGeneration) return;
    m_analysis = std::move(analysis);
    setBusy(false);
    if (m_analysis.loaded && !m_outputEditedByUser) {
        m_outputEdit->setText(SiestaFinalConverter::suggestedOutputPath(
            m_analysis.sourcePath, m_analysis.structure.title));
    }
    updatePreview();
    if (!m_analysis.loaded) {
        showStatus(m_analysis.errorMessage.isEmpty()
            ? (m_japanese ? QStringLiteral("最終構造を読み込めませんでした。") : QStringLiteral("Could not load the final geometry."))
            : m_analysis.errorMessage, true);
    } else if (!m_analysis.readyToWrite()) {
        showStatus(m_japanese
            ? QStringLiteral("最終座標は読み込めましたが物理セルがありません。［変更…］からFDF/VASPを選択してください。")
            : QStringLiteral("Coordinates were loaded, but no physical cell was found. Choose an FDF/VASP file with Change…"), true);
        m_cellButton->setFocus();
    } else if (m_analysis.cellSourcePath == m_analysis.sourcePath) {
        showStatus(m_japanese ? QStringLiteral("最終構造と埋め込みセルを読み込みました。") : QStringLiteral("Loaded the final geometry and embedded cell."));
    } else {
        showStatus(m_japanese
            ? QStringLiteral("同じフォルダーから格子情報を自動検出しました。")
            : QStringLiteral("Cell information was detected from the same folder."));
    }
}

void SiestaFinalConversionDialog::chooseCell() {
    const QString path = QFileDialog::getOpenFileName(this,
        m_japanese ? QStringLiteral("格子情報を含むFDF/VASPファイルを選択") : QStringLiteral("Select an FDF/VASP file containing the cell"),
        m_initialDirectory,
        QStringLiteral("Cell files (*.fdf *.out *.txt *.vasp *.poscar POSCAR CONTCAR);;All files (*.*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!SiestaFinalConverter().applyCellFile(&m_analysis, path, &error)) {
        showStatus(error, true);
        return;
    }
    updatePreview();
    showStatus(m_japanese ? QStringLiteral("指定した格子情報を適用しました。") : QStringLiteral("Applied the selected cell."));
}

void SiestaFinalConversionDialog::chooseOutput() {
    QString path = QFileDialog::getSaveFileName(this,
        m_japanese ? QStringLiteral("VASPファイルを保存") : QStringLiteral("Save VASP file"),
        m_outputEdit->text().isEmpty() ? m_initialDirectory : m_outputEdit->text(),
        QStringLiteral("VASP POSCAR (*.vasp *.poscar POSCAR CONTCAR)"));
    if (path.isEmpty()) return;
    m_outputEditedByUser = true;
    if (QFileInfo(path).suffix().isEmpty()
        && QFileInfo(path).fileName().compare(QStringLiteral("POSCAR"), Qt::CaseInsensitive) != 0
        && QFileInfo(path).fileName().compare(QStringLiteral("CONTCAR"), Qt::CaseInsensitive) != 0) {
        path += QStringLiteral(".vasp");
    }
    m_outputEdit->setText(path);
}

void SiestaFinalConversionDialog::updatePreview() {
    const QString dash = QStringLiteral("—");
    if (!m_analysis.loaded) {
        m_atomValue->setText(dash); m_elementValue->setText(dash); m_fixedValue->setText(dash); m_cellValue->setText(dash);
        m_cellEdit->clear();
        m_convertButton->setEnabled(false);
        return;
    }
    m_atomValue->setText(QString::number(m_analysis.structure.atoms.size()));
    QStringList elements;
    int fixed = 0;
    for (const NativeAtom& atom : m_analysis.structure.atoms) {
        if (!elements.contains(atom.element)) elements << atom.element;
        if (std::any_of(atom.movable.begin(), atom.movable.end(), [](bool movable) { return !movable; })) ++fixed;
    }
    m_elementValue->setText(elements.join(QStringLiteral(", ")));
    m_fixedValue->setText(QString::number(fixed));
    if (m_analysis.readyToWrite()) {
        const auto& cell = m_analysis.structure.cellVectors;
        m_cellValue->setText(QStringLiteral("a %1   b %2   c %3")
            .arg(cell[0].length(), 0, 'f', 4).arg(cell[1].length(), 0, 'f', 4).arg(cell[2].length(), 0, 'f', 4));
        m_cellEdit->setText(m_analysis.cellSourcePath);
    } else {
        m_cellValue->setText(m_japanese ? QStringLiteral("未設定") : QStringLiteral("Not set"));
        m_cellEdit->clear();
    }
    m_convertButton->setEnabled(m_analysis.readyToWrite() && !m_outputEdit->text().trimmed().isEmpty());
}

void SiestaFinalConversionDialog::setBusy(bool busy) {
    m_progress->setVisible(busy);
    m_inputButton->setEnabled(!busy);
    m_cellButton->setEnabled(!busy && m_analysis.loaded);
    m_outputButton->setEnabled(!busy);
    m_convertButton->setEnabled(!busy && m_analysis.readyToWrite() && !m_outputEdit->text().trimmed().isEmpty());
}

void SiestaFinalConversionDialog::showStatus(const QString& text, bool error) {
    m_statusLabel->setText(text);
    const QColor color = error ? palette().color(QPalette::BrightText) : palette().color(QPalette::Text);
    const QColor background = error ? QColor(120, 45, 35) : palette().color(QPalette::AlternateBase);
    m_statusLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; background: %2; border-radius: 5px; padding: 8px 10px; }")
        .arg(color.name(), background.name()));
}

void SiestaFinalConversionDialog::convertAndAccept() {
    QString output = m_outputEdit->text().trimmed();
    if (output.isEmpty() || !m_analysis.readyToWrite()) return;
    if (QFileInfo::exists(output)) {
        const auto choice = QMessageBox::question(this,
            m_japanese ? QStringLiteral("上書き確認") : QStringLiteral("Confirm overwrite"),
            m_japanese ? QStringLiteral("既存ファイルを上書きしますか？\n%1").arg(output)
                       : QStringLiteral("Overwrite the existing file?\n%1").arg(output),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice != QMessageBox::Yes) return;
    }
    const auto mode = static_cast<StructureCoordinateMode>(m_coordinateCombo->currentData().toInt());
    QString error;
    if (!SiestaFinalConverter().writeVasp(m_analysis, output, mode, &error)) {
        showStatus(error, true);
        return;
    }
    m_writtenOutputPath = QFileInfo(output).absoluteFilePath();
    accept();
}

void SiestaFinalConversionDialog::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()
        && !event->mimeData()->urls().isEmpty()
        && event->mimeData()->urls().constFirst().isLocalFile()) {
        event->acceptProposedAction();
    }
}

void SiestaFinalConversionDialog::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty() || !urls.constFirst().isLocalFile()) return;
    setInputPath(urls.constFirst().toLocalFile());
    event->acceptProposedAction();
}
