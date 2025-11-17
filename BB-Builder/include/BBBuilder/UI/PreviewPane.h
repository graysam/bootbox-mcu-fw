#pragma once

#include <QJsonObject>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QWebEngineView;
QT_END_NAMESPACE

namespace BBB {

class PreviewPane : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPane(QWidget *parent = nullptr);

    void setSchema(const QJsonObject &schema);

private slots:
    void handleLoadFinished(bool ok);

private:
    void flushPending();

    QWebEngineView *view_ = nullptr;
    bool loaded_ = false;
    QJsonObject pendingSchema_;
};

} // namespace BBB
