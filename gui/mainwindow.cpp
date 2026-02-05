#include "mainwindow.hpp"

#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
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

    // Environment row
    auto* envLayout = new QHBoxLayout();
    m_includeEnvCheck = new QCheckBox("Include environment:");
    m_includeEnvCheck->setChecked(true);
    envLayout->addWidget(m_includeEnvCheck);
    m_envPathEdit = new QLineEdit();
    m_envPathEdit->setPlaceholderText("Path to environment.ply...");
    envLayout->addWidget(m_envPathEdit, 1);
    m_browseEnvBtn = new QPushButton("Browse...");
    envLayout->addWidget(m_browseEnvBtn);
    settingsLayout->addLayout(envLayout);

    // Collision row
    auto* collisionLayout = new QHBoxLayout();
    m_includeCollisionCheck = new QCheckBox("Include collision:");
    m_includeCollisionCheck->setChecked(false);
    collisionLayout->addWidget(m_includeCollisionCheck);
    m_collisionPathEdit = new QLineEdit();
    m_collisionPathEdit->setPlaceholderText("Path to collision.ply...");
    collisionLayout->addWidget(m_collisionPathEdit, 1);
    m_browseCollisionBtn = new QPushButton("Browse...");
    collisionLayout->addWidget(m_browseCollisionBtn);
    settingsLayout->addLayout(collisionLayout);

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
    connect(m_browseEnvBtn, &QPushButton::clicked, this, &MainWindow::browseEnv);
    connect(m_browseCollisionBtn, &QPushButton::clicked, this, &MainWindow::browseCollision);
    connect(m_convertBtn, &QPushButton::clicked, this, &MainWindow::startConversion);
    connect(m_inputPathEdit, &QLineEdit::textChanged, this, &MainWindow::updateConvertButtonState);
    connect(m_inputPathEdit, &QLineEdit::textChanged, this, &MainWindow::onInputPathChanged);
    connect(m_outputDirEdit, &QLineEdit::textChanged, this, &MainWindow::updateConvertButtonState);

    // Environment and collision path change handlers
    connect(m_envPathEdit, &QLineEdit::textChanged, this, &MainWindow::onEnvPathChanged);
    connect(m_collisionPathEdit, &QLineEdit::textChanged, this, &MainWindow::onCollisionPathChanged);
}

void MainWindow::browseInput() {
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "Select Point Cloud PLY File",
        QString(),
        "Point Cloud (point_cloud*.ply);;All Files (*)");
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

void MainWindow::browseEnv() {
    QString startDir = getInputDir();
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "Select Environment PLY File",
        startDir,
        "PLY Files (*.ply);;All Files (*)");
    if (!filePath.isEmpty()) {
        m_envPathEdit->setText(filePath);
    }
}

void MainWindow::browseCollision() {
    QString startDir = getInputDir();
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "Select Collision PLY File",
        startDir,
        "PLY Files (*.ply);;All Files (*)");
    if (!filePath.isEmpty()) {
        m_collisionPathEdit->setText(filePath);
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
        m_includeEnvCheck->isChecked(),
        m_envPathEdit->text(),
        m_includeCollisionCheck->isChecked(),
        m_collisionPathEdit->text());
}

void MainWindow::updateConvertButtonState() {
    bool canConvert = !m_inputPathEdit->text().isEmpty() &&
                      !m_outputDirEdit->text().isEmpty();
    m_convertBtn->setEnabled(canConvert);
}

void MainWindow::onInputPathChanged(const QString& path) {
    Q_UNUSED(path);
    QString dir = getInputDir();
    if (dir.isEmpty()) {
        return;
    }

    // Update environment default path if empty or was a default path
    QString defaultEnvPath = QDir(dir).filePath("environment.ply");
    QString currentEnvPath = m_envPathEdit->text();
    if (currentEnvPath.isEmpty() || currentEnvPath.endsWith("/environment.ply")) {
        m_envPathEdit->setText(defaultEnvPath);
    }

    // Update collision default path if empty or was a default path
    QString defaultCollPath = QDir(dir).filePath("collision.ply");
    QString currentCollPath = m_collisionPathEdit->text();
    if (currentCollPath.isEmpty() || currentCollPath.endsWith("/collision.ply")) {
        m_collisionPathEdit->setText(defaultCollPath);
    }
}

void MainWindow::onEnvPathChanged(const QString& path) {
    bool exists = !path.isEmpty() && QFileInfo::exists(path);
    updatePathStyle(m_envPathEdit, exists);
}

void MainWindow::onCollisionPathChanged(const QString& path) {
    bool exists = !path.isEmpty() && QFileInfo::exists(path);
    updatePathStyle(m_collisionPathEdit, exists);
}

QString MainWindow::getInputDir() const {
    QString inputPath = m_inputPathEdit->text();
    if (inputPath.isEmpty()) {
        return QString();
    }

    QFileInfo fileInfo(inputPath);
    if (fileInfo.isDir()) {
        return inputPath;
    } else {
        return fileInfo.absolutePath();
    }
}

void MainWindow::updatePathStyle(QLineEdit* edit, bool exists) {
    if (exists) {
        edit->setStyleSheet("");
    } else {
        edit->setStyleSheet("QLineEdit { background-color: #ffcccc; }");
    }
}

void MainWindow::setInputsEnabled(bool enabled) {
    m_inputPathEdit->setEnabled(enabled);
    m_outputDirEdit->setEnabled(enabled);
    m_browseInputBtn->setEnabled(enabled);
    m_browseOutputBtn->setEnabled(enabled);
    m_cellSizeXSpin->setEnabled(enabled);
    m_cellSizeYSpin->setEnabled(enabled);
    m_singleLodCheck->setEnabled(enabled);

    // Environment controls
    m_includeEnvCheck->setEnabled(enabled);
    m_envPathEdit->setEnabled(enabled);
    m_browseEnvBtn->setEnabled(enabled);

    // Collision controls
    m_includeCollisionCheck->setEnabled(enabled);
    m_collisionPathEdit->setEnabled(enabled);
    m_browseCollisionBtn->setEnabled(enabled);

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
