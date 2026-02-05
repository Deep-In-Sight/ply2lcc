#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>

class QLineEdit;
class QPushButton;
class QDoubleSpinBox;
class QCheckBox;
class QTextEdit;
class QProgressBar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

signals:
    void conversionRequested(const QString& inputPath,
                             const QString& outputDir,
                             double cellSizeX,
                             double cellSizeY,
                             bool singleLod,
                             bool includeEnvironment,
                             const QString& envPath,
                             bool includeCollision,
                             const QString& collisionPath);

public slots:
    void onProgressChanged(int percent);
    void onLogMessage(const QString& message);
    void onConversionFinished(bool success, const QString& error);

private slots:
    void browseInput();
    void browseOutput();
    void browseEnv();
    void browseCollision();
    void startConversion();
    void updateConvertButtonState();
    void onInputPathChanged(const QString& path);
    void onEnvPathChanged(const QString& path);
    void onCollisionPathChanged(const QString& path);

private:
    void setupUi();
    void setInputsEnabled(bool enabled);
    void updatePathStyle(QLineEdit* edit, bool exists);
    QString getInputDir() const;

    // Input/Output widgets
    QLineEdit* m_inputPathEdit;
    QLineEdit* m_outputDirEdit;
    QPushButton* m_browseInputBtn;
    QPushButton* m_browseOutputBtn;

    // Settings widgets
    QDoubleSpinBox* m_cellSizeXSpin;
    QDoubleSpinBox* m_cellSizeYSpin;
    QCheckBox* m_singleLodCheck;
    QCheckBox* m_includeEnvCheck;
    QLineEdit* m_envPathEdit;
    QPushButton* m_browseEnvBtn;
    QCheckBox* m_includeCollisionCheck;
    QLineEdit* m_collisionPathEdit;
    QPushButton* m_browseCollisionBtn;

    // Log and progress widgets
    QTextEdit* m_logEdit;
    QProgressBar* m_progressBar;
    QPushButton* m_convertBtn;
};

#endif // MAINWINDOW_HPP
