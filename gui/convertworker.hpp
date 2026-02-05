#ifndef CONVERTWORKER_HPP
#define CONVERTWORKER_HPP

#include <QObject>
#include <QString>

class ConvertWorker : public QObject {
    Q_OBJECT

public:
    explicit ConvertWorker(QObject *parent = nullptr);
    ~ConvertWorker() override = default;

public slots:
    void process();

signals:
    void finished();
    void error(const QString &message);
    void progress(int percent);
};

#endif // CONVERTWORKER_HPP
