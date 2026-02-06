#include <QApplication>
#include "mainwindow.hpp"
#include "convertworker.hpp"
#include "types.hpp"
#include <filesystem>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ply2lcc Converter");

    MainWindow window;

    QObject::connect(&window, &MainWindow::conversionRequested,
        [&window](const QString& inputPath, const QString& outputDir,
                  double cellX, double cellY, bool singleLod,
                  bool includeEnv, const QString& envPath,
                  bool includeCollision, const QString& collisionPath) {

            ply2lcc::ConvertConfig config;
            config.input_path = std::filesystem::u8path(inputPath.toStdString());
            config.output_dir = std::filesystem::u8path(outputDir.toStdString());
            config.cell_size_x = static_cast<float>(cellX);
            config.cell_size_y = static_cast<float>(cellY);
            config.single_lod = singleLod;
            config.include_env = includeEnv;
            config.env_path = std::filesystem::u8path(envPath.toStdString());
            config.include_collision = includeCollision;
            config.collision_path = std::filesystem::u8path(collisionPath.toStdString());

            auto* worker = new ConvertWorker(config, &window);

            QObject::connect(worker, &ConvertWorker::progressChanged,
                           &window, &MainWindow::onProgressChanged);
            QObject::connect(worker, &ConvertWorker::logMessage,
                           &window, &MainWindow::onLogMessage);
            QObject::connect(worker, &ConvertWorker::finished,
                           &window, &MainWindow::onConversionFinished);

            // Clean up worker when thread actually finishes (QThread::finished is emitted after run() returns)
            QObject::connect(worker, &QThread::finished,
                           worker, &QObject::deleteLater);

            worker->start();
        });

    window.show();
    return app.exec();
}
