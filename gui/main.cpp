#include <QApplication>
#include "mainwindow.hpp"
#include "convertworker.hpp"
#include "types.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ply2lcc Converter");

    MainWindow window;

    QObject::connect(&window, &MainWindow::conversionRequested,
        [&window](const QString& inputPath, const QString& outputDir,
                  double cellX, double cellY, bool singleLod, bool includeEnv) {

            ply2lcc::ConvertConfig config;
            config.input_path = inputPath.toStdString();
            config.output_dir = outputDir.toStdString();
            config.cell_size_x = static_cast<float>(cellX);
            config.cell_size_y = static_cast<float>(cellY);
            config.single_lod = singleLod;
            config.include_env = includeEnv;

            auto* worker = new ConvertWorker(config, &window);

            QObject::connect(worker, &ConvertWorker::progressChanged,
                           &window, &MainWindow::onProgressChanged);
            QObject::connect(worker, &ConvertWorker::logMessage,
                           &window, &MainWindow::onLogMessage);
            QObject::connect(worker, &ConvertWorker::finished,
                           &window, &MainWindow::onConversionFinished);

            // Clean up worker when done - wait for thread to finish first
            QObject::connect(worker, &ConvertWorker::finished,
                           [worker](bool, const QString&) {
                               worker->wait();
                               worker->deleteLater();
                           });

            worker->start();
        });

    window.show();
    return app.exec();
}
