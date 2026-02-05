#include "convertworker.hpp"
#include "convert_app.hpp"
#include <QDateTime>

ConvertWorker::ConvertWorker(const ply2lcc::ConvertConfig& config, QObject* parent)
    : QThread(parent), config_(config) {}

void ConvertWorker::run() {
    try {
        ply2lcc::ConvertApp app(config_);

        // Route progress updates to GUI
        app.setProgressCallback([this](int percent, const std::string& msg) {
            emit progressChanged(percent);
            QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss] ");
            emit logMessage(timestamp + QString::fromStdString(msg));
        });

        // Route console output to GUI log
        app.setLogCallback([this](const std::string& msg) {
            emit logMessage(QString::fromStdString(msg));
        });

        app.run();
        emit finished(true, QString());
    } catch (const std::exception& e) {
        emit finished(false, QString::fromStdString(e.what()));
    }
}
