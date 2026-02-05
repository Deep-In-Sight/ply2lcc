#include <QApplication>
#include "mainwindow.hpp"
#include "convertworker.hpp"
#include "types.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ply2lcc Converter");

    MainWindow window;

    // Connect MainWindow's conversionRequested signal to create and start worker
    QObject::connect(&window, &MainWindow::conversionRequested,
        [&window](const QString& inputPath, const QString& outputDir,
                  float cellX, float cellY, bool singleLod, bool includeEnv) {

            ply2lcc::ConvertConfig config;
            config.input_path = inputPath.toStdString();
            config.output_dir = outputDir.toStdString();
            config.cell_size_x = cellX;
            config.cell_size_y = cellY;
            config.single_lod = singleLod;
            config.include_env = includeEnv;

            auto* worker = new ConvertWorker(config, &window);

            QObject::connect(worker, &ConvertWorker::progressChanged,
                           &window, &MainWindow::onProgressChanged);
            QObject::connect(worker, &ConvertWorker::logMessage,
                           &window, &MainWindow::onLogMessage);
            QObject::connect(worker, &ConvertWorker::finished,
                           &window, &MainWindow::onConversionFinished);

            // Clean up worker when done
            QObject::connect(worker, &ConvertWorker::finished,
                           worker, &QObject::deleteLater);

            worker->start();
        });

    window.show();
    return app.exec();
}
