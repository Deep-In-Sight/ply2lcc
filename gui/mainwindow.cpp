#include "mainwindow.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QTime>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle("ply2lcc Converter");
    resize(700, 550);
    setupUi();
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // Input PLY row
    auto* inputLayout = new QHBoxLayout();
    inputLayout->addWidget(new QLabel("Input PLY:"));
    m_inputPathEdit = new QLineEdit();
    m_inputPathEdit->setPlaceholderText("Select a PLY file...");
    inputLayout->addWidget(m_inputPathEdit, 1);
    m_browseInputBtn = new QPushButton("Browse...");
    inputLayout->addWidget(m_browseInputBtn);
    mainLayout->addLayout(inputLayout);

    // Output Dir row
    auto* outputLayout = new QHBoxLayout();
    outputLayout->addWidget(new QLabel("Output Dir:"));
    m_outputDirEdit = new QLineEdit();
    m_outputDirEdit->setPlaceholderText("Select output directory...");
    outputLayout->addWidget(m_outputDirEdit, 1);
    m_browseOutputBtn = new QPushButton("Browse...");
    outputLayout->addWidget(m_browseOutputBtn);
    mainLayout->addLayout(outputLayout);

    // Settings group
    auto* settingsGroup = new QGroupBox("Settings");
    auto* settingsLayout = new QVBoxLayout(settingsGroup);

    // Cell size row
    auto* cellSizeLayout = new QHBoxLayout();
    cellSizeLayout->addWidget(new QLabel("Cell Size X:"));
    m_cellSizeXSpin = new QDoubleSpinBox();
    m_cellSizeXSpin->setRange(1.0, 1000.0);
    m_cellSizeXSpin->setValue(30.0);
    m_cellSizeXSpin->setDecimals(1);
    cellSizeLayout->addWidget(m_cellSizeXSpin);
    cellSizeLayout->addSpacing(20);
    cellSizeLayout->addWidget(new QLabel("Cell Size Y:"));
    m_cellSizeYSpin = new QDoubleSpinBox();
    m_cellSizeYSpin->setRange(1.0, 1000.0);
    m_cellSizeYSpin->setValue(30.0);
    m_cellSizeYSpin->setDecimals(1);
    cellSizeLayout->addWidget(m_cellSizeYSpin);
    cellSizeLayout->addStretch();
    settingsLayout->addLayout(cellSizeLayout);

    // Checkboxes
    m_singleLodCheck = new QCheckBox("Single LOD mode");
    settingsLayout->addWidget(m_singleLodCheck);

    m_includeEnvCheck = new QCheckBox("Include environment");
    m_includeEnvCheck->setChecked(true);
    settingsLayout->addWidget(m_includeEnvCheck);

    m_includeCollisionCheck = new QCheckBox("Include collision");
    m_includeCollisionCheck->setEnabled(false);
    m_includeCollisionCheck->setToolTip("Coming soon");
    settingsLayout->addWidget(m_includeCollisionCheck);

    mainLayout->addWidget(settingsGroup);

    // Log area
    auto* logLabel = new QLabel("Log:");
    mainLayout->addWidget(logLabel);
    m_logEdit = new QTextEdit();
    m_logEdit->setReadOnly(true);
    m_logEdit->setMinimumHeight(150);
    mainLayout->addWidget(m_logEdit, 1);

    // Progress bar
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    mainLayout->addWidget(m_progressBar);

    // Convert button (centered)
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_convertBtn = new QPushButton("Convert");
    m_convertBtn->setMinimumWidth(120);
    m_convertBtn->setEnabled(false);
    buttonLayout->addWidget(m_convertBtn);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    setCentralWidget(centralWidget);

    // Connect signals
    connect(m_browseInputBtn, &QPushButton::clicked, this, &MainWindow::browseInput);
    connect(m_browseOutputBtn, &QPushButton::clicked, this, &MainWindow::browseOutput);
    connect(m_convertBtn, &QPushButton::clicked, this, &MainWindow::startConversion);
    connect(m_inputPathEdit, &QLineEdit::textChanged, this, &MainWindow::updateConvertButtonState);
    connect(m_outputDirEdit, &QLineEdit::textChanged, this, &MainWindow::updateConvertButtonState);
}

void MainWindow::browseInput() {
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "Select PLY File",
        QString(),
        "PLY Files (*.ply);;All Files (*)");
    if (!filePath.isEmpty()) {
        m_inputPathEdit->setText(filePath);
    }
}

void MainWindow::browseOutput() {
    QString dirPath = QFileDialog::getExistingDirectory(
        this,
        "Select Output Directory",
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dirPath.isEmpty()) {
        m_outputDirEdit->setText(dirPath);
    }
}

void MainWindow::startConversion() {
    setInputsEnabled(false);
    m_progressBar->setValue(0);
    m_logEdit->clear();

    QString timestamp = QTime::currentTime().toString("hh:mm:ss");
    m_logEdit->append(QString("[%1] Starting conversion...").arg(timestamp));

    emit conversionRequested(
        m_inputPathEdit->text(),
        m_outputDirEdit->text(),
        m_cellSizeXSpin->value(),
        m_cellSizeYSpin->value(),
        m_singleLodCheck->isChecked(),
        m_includeEnvCheck->isChecked());
}

void MainWindow::updateConvertButtonState() {
    bool canConvert = !m_inputPathEdit->text().isEmpty() &&
                      !m_outputDirEdit->text().isEmpty();
    m_convertBtn->setEnabled(canConvert);
}

void MainWindow::setInputsEnabled(bool enabled) {
    m_inputPathEdit->setEnabled(enabled);
    m_outputDirEdit->setEnabled(enabled);
    m_browseInputBtn->setEnabled(enabled);
    m_browseOutputBtn->setEnabled(enabled);
    m_cellSizeXSpin->setEnabled(enabled);
    m_cellSizeYSpin->setEnabled(enabled);
    m_singleLodCheck->setEnabled(enabled);
    m_includeEnvCheck->setEnabled(enabled);
    // m_includeCollisionCheck stays disabled (coming soon)
    m_convertBtn->setEnabled(enabled && !m_inputPathEdit->text().isEmpty() &&
                             !m_outputDirEdit->text().isEmpty());
}

void MainWindow::onProgressChanged(int percent) {
    m_progressBar->setValue(percent);
}

void MainWindow::onLogMessage(const QString& message) {
    QString timestamp = QTime::currentTime().toString("hh:mm:ss");
    m_logEdit->append(QString("[%1] %2").arg(timestamp, message));
}

void MainWindow::onConversionFinished(bool success, const QString& error) {
    setInputsEnabled(true);

    QString timestamp = QTime::currentTime().toString("hh:mm:ss");
    if (success) {
        m_progressBar->setValue(100);
        m_logEdit->append(QString("[%1] <span style=\"color: green;\">Conversion completed successfully!</span>").arg(timestamp));
    } else {
        m_logEdit->append(QString("[%1] <span style=\"color: red;\">Error: %2</span>").arg(timestamp, error));
    }
}
