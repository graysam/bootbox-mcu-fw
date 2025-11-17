#include <BBBuilder/UI/PreviewPane.h>

#include <QJsonDocument>
#include <QVBoxLayout>
#include <QWebEngineView>
#include <QWebEngineSettings>

namespace BBB {

PreviewPane::PreviewPane(QWidget *parent)
    : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    view_ = new QWebEngineView(this);
    view_->setContextMenuPolicy(Qt::NoContextMenu);
    view_->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    layout->addWidget(view_);
    connect(view_, &QWebEngineView::loadFinished, this, &PreviewPane::handleLoadFinished);
    view_->setUrl(QUrl(QStringLiteral("qrc:/preview/preview.html")));
}

void PreviewPane::setSchema(const QJsonObject &schema) {
    pendingSchema_ = schema;
    if (loaded_) {
        flushPending();
    }
}

void PreviewPane::handleLoadFinished(bool ok) {
    loaded_ = ok;
    if (loaded_) {
        flushPending();
    }
}

void PreviewPane::flushPending() {
    if (!view_ || pendingSchema_.isEmpty()) return;
    const QByteArray json = QJsonDocument(pendingSchema_).toJson(QJsonDocument::Compact);
    const QString script = QStringLiteral("window.renderPreview(%1);")
                               .arg(QString::fromUtf8(json));
    view_->page()->runJavaScript(script);
}

} // namespace BBB
