#include "remoteimage.h"
#include "northapi.h"
#include "windowchrome.h"
#include "scrollbehavior.h"
#include <QBuffer>
#include <QDialog>
#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>
#include <algorithm>

namespace {
void paintImage(QPainter &p, const QRect &rect, const QImage &image)
{
    const auto size = image.size().scaled(rect.size(), Qt::KeepAspectRatio);
    const QRect target(rect.center() - QPoint(size.width() / 2, size.height() / 2), size);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(target, image);
}
class ImageCanvas : public QWidget {
public:
    ImageCanvas(QImage image, QWidget *parent) : QWidget(parent), m_image(std::move(image)) {}
protected:
    void paintEvent(QPaintEvent *) override { QPainter painter(this); paintImage(painter, rect(), m_image); }
private:
    QImage m_image;
};
}

RemoteImage::RemoteImage(NorthApi *api, QUrl url, const QString &altText, QWidget *parent)
    : QAbstractButton(parent), m_api(api), m_url(std::move(url))
{
    setObjectName("remoteImage");
    setAccessibleName(altText.isEmpty() ? tr("画像") : altText);
    setToolTip(altText);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setMinimumHeight(120);
    connect(this, &QAbstractButton::clicked, this, [this] {
        if (!m_error.isEmpty()) { m_started = false; m_error.clear(); load(); }
        else if (m_preview) showPreview();
    });
    if (m_api) connect(m_api, &NorthApi::loggedOut, this, &RemoteImage::clear);
}

QSize RemoteImage::sizeHint() const { return {450, heightForWidth(450)}; }

int RemoteImage::heightForWidth(int width) const
{
    const auto size = m_expectedSize.isValid() ? m_expectedSize : m_image.size();
    const double ratio = size.isValid() ? double(size.height()) / size.width() : 0.55;
    return std::clamp(int(width * ratio), 120, 510);
}

void RemoteImage::clear()
{
    m_image = {};
    m_error = tr("ログインしてください");
    m_started = false;
    update();
}

void RemoteImage::showEvent(QShowEvent *event)
{
    QAbstractButton::showEvent(event);
    if (!m_started) load();
}

void RemoteImage::load()
{
    m_started = true;
    if (!m_api) { m_error = tr("画像を読み込めませんでした"); emit failed(); update(); return; }
    m_api->fetchMedia(this, m_url, [this](MediaResult result) {
        if (!result.ok()) { m_error = result.error; emit failed(); update(); return; }
        decode(result.bytes);
    });
}

void RemoteImage::decode(const QByteArray &bytes)
{
    QScrollArea *area = nullptr;
    if (!m_expectedSize.isValid() && minimumHeight() != maximumHeight())
        for (auto *p = parentWidget(); p; p = p->parentWidget()) if ((area = qobject_cast<QScrollArea *>(p))) break;
    const ScrollAnchorGuard keepPosition(area);
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const auto format = reader.format().toLower();
    const auto size = reader.size();
    const bool supported = QList<QByteArray>{"png", "jpeg", "jpg", "webp", "gif", "avif", "bmp"}.contains(format);
    if (!supported || !size.isValid() || qint64(size.width()) * size.height() > 40'000'000) {
        m_error = tr("この画像は表示できません"); emit failed(); update(); return;
    }
    if (size.width() > 4096 || size.height() > 4096) reader.setScaledSize(size.scaled(4096, 4096, Qt::KeepAspectRatio));
    m_image = reader.read();
    if (m_image.isNull()) { m_error = tr("画像を読み込めませんでした"); emit failed(); }
    else { m_error.clear(); emit loaded(); }
    updateGeometry();
    update();
}

void RemoteImage::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    const qreal radius = m_circular ? std::min(width(), height()) / 2.0 : m_radius;
    clip.addRoundedRect(QRectF(rect()), radius, radius);
    painter.setClipPath(clip);
    painter.fillRect(rect(), palette().color(QPalette::AlternateBase));
    if (!m_image.isNull()) {
        if (m_crop || m_circular) {
            const auto size = m_image.size().scaled(rect().size(), Qt::KeepAspectRatioByExpanding);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QRect(rect().center() - QPoint(size.width() / 2, size.height() / 2), size), m_image);
        } else paintImage(painter, rect(), m_image);
        if (m_playIndicator) {
            painter.save();
            const QPointF center = QRectF(rect()).center();
            painter.setPen(Qt::NoPen); painter.setBrush(QColor("#e62117"));
            painter.drawRoundedRect(QRectF(center.x() - 24, center.y() - 16, 48, 32), 7, 7);
            QPainterPath triangle;
            triangle.moveTo(center.x() - 5, center.y() - 8); triangle.lineTo(center.x() + 8, center.y());
            triangle.lineTo(center.x() - 5, center.y() + 8); triangle.closeSubpath();
            painter.fillPath(triangle, Qt::white); painter.restore();
        }
    } else if (m_circular || width() <= 48) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, accessibleName().left(1).toUpper());
    } else {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap,
                         m_error.isEmpty() ? tr("読み込み中…") : m_error + tr("\n再試行"));
    }
    if (hasFocus()) {
        painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 6, 6);
    }
}

void RemoteImage::showPreview()
{
    if (m_image.isNull() || m_viewing) return;
    auto *dialog = new QDialog(window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("画像"));
    dialog->setObjectName("imageViewer");
    const auto screenSize = screen()->availableGeometry().size() * 0.85;
    const auto target = m_image.size().scaled(screenSize, Qt::KeepAspectRatio);
    dialog->resize(target.expandedTo(QSize(320, 240)));
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new WindowTitleBar(dialog, false));
    layout->addWidget(new ImageCanvas(m_image, dialog));
    if (m_api) connect(m_api, &NorthApi::loggedOut, dialog, &QDialog::reject);
    m_viewing = true;
    connect(dialog, &QDialog::finished, this, [this] { m_viewing = false; });
    // A viewer must not outlive the post/conversation that owns its image.
    connect(this, &QObject::destroyed, dialog, &QDialog::reject);
    dialog->open();
}
