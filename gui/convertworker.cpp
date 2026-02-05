#include "convertworker.hpp"

ConvertWorker::ConvertWorker(QObject *parent)
    : QObject(parent) {
}

void ConvertWorker::process() {
    // Placeholder - conversion logic will be implemented later
    emit finished();
}
