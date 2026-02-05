#ifndef CONVERTWORKER_HPP
#define CONVERTWORKER_HPP

#include <QThread>
#include <QString>
#include "types.hpp"

class ConvertWorker : public QThread {
    Q_OBJECT
public:
    explicit ConvertWorker(const ply2lcc::ConvertConfig& config, QObject* parent = nullptr);

signals:
    void progressChanged(int percent);
    void logMessage(const QString& message);
    void finished(bool success, const QString& error);

protected:
    void run() override;

private:
    ply2lcc::ConvertConfig config_;
};

#endif
