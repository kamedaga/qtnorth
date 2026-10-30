#pragma once
#include <QAbstractButton>
#include <QImage>
#include <QPointer>
#include <QUrl>
class NorthApi;

class RemoteImage : public QAbstractButton {
    Q_OBJECT
public:
    RemoteImage(NorthApi *api, QUrl url, const QString &altText = {}, QWidget *parent = nullptr);
    bool hasImage() const { return !m_image.isNull(); }
    QSize sizeHint() const override;
    int heightForWidth(int width) const override;
    void setPreviewEnabled(bool enabled) { m_preview = enabled; }
    void setCircular(bool circular) { m_circular = circular; update(); }
    void setCropToFill(bool crop) { m_crop = crop; update(); }
    void setCornerRadius(qreal radius) { m_radius = radius; update(); }
    void setPlayIndicator(bool enabled) { m_playIndicator = enabled; update(); }
    bool playIndicator() const { return m_playIndicator; }
    void setImageSize(const QSize &size) { m_expectedSize = size; updateGeometry(); }
signals:
    void loaded();
    void failed();
protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *event) override;
private:
    void load();
    void decode(const QByteArray &bytes);
    void showPreview();
    void clear();
    QPointer<NorthApi> m_api;
    QUrl m_url;
    QImage m_image;
    QSize m_expectedSize;
    QString m_error;
    bool m_started = false, m_preview = true, m_viewing = false, m_circular = false;
    bool m_crop = false;
    bool m_playIndicator = false;
    qreal m_radius = 12;
};
