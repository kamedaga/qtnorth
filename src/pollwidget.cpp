#include "pollwidget.h"
#include "northapi.h"
#include "scrollbehavior.h"
#include <QButtonGroup>
#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>

namespace {
class PollChoice : public QWidget {
public:
    explicit PollChoice(QRadioButton *radio, const QString &label) : m_radio(radio) {
        auto *row = new QHBoxLayout(this); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(6);
        row->addWidget(radio, 0, Qt::AlignTop);
        auto *text = new QLabel(label); text->setTextFormat(Qt::PlainText); text->setWordWrap(true); text->setMinimumWidth(0);
        row->addWidget(text, 1); setCursor(Qt::PointingHandCursor);
    }
protected:
    void mousePressEvent(QMouseEvent *event) override { if (event->button() == Qt::LeftButton) { if (m_radio->isEnabled()) m_radio->setChecked(true); event->accept(); } else QWidget::mousePressEvent(event); }
    void mouseReleaseEvent(QMouseEvent *event) override { if (event->button() == Qt::LeftButton) event->accept(); else QWidget::mouseReleaseEvent(event); }
private:
    QRadioButton *m_radio;
};
QLabel *plain(const QString &value, const char *name) {
    auto *label = new QLabel(value); label->setObjectName(name);
    label->setTextFormat(Qt::PlainText); label->setWordWrap(true); return label;
}
}

PollWidget::PollWidget(QString postId, QJsonObject poll, bool interactive, NorthApi *api, QWidget *parent)
    : QFrame(parent), m_postId(std::move(postId)), m_poll(std::move(poll)), m_interactive(interactive), m_api(api)
{
    setObjectName("pollWidget"); setProperty("postId", m_postId); setCursor(Qt::ArrowCursor);
    m_layout = new QVBoxLayout(this); m_layout->setContentsMargins(0, 6, 0, 6); m_layout->setSpacing(6);
    m_expiry.setSingleShot(true); m_expiry.setTimerType(Qt::PreciseTimer);
    connect(&m_expiry, &QTimer::timeout, this, [this] {
        m_awaitingResults = true; render(); if (m_api) m_api->refreshPoll(m_postId);
    });
    if (api) {
        connect(api, &NorthApi::pollUpdated, this, [this](const QString &id, const QJsonObject &state) { if (id == m_postId) setPoll(state); });
        connect(api, &NorthApi::pollBusyChanged, this, [this](const QString &id, bool) { if (id == m_postId) updateControls(); });
        connect(api, &NorthApi::pollFailed, this, [this](const QString &id, const QString &message) {
            if (id != m_postId || !m_error) return;
            m_error->setText(message); m_error->show(); updateControls();
        });
        connect(api, &NorthApi::loggedOut, this, [this] { m_interactive = false; m_error->clear(); m_error->hide(); updateControls(); });
    }
    render();
}

bool PollWidget::ended() const
{
    const auto end = QDateTime::fromString(m_poll.value("endsAt").toString(), Qt::ISODate);
    return m_poll.value("ended").toBool() || !end.isValid() || end <= QDateTime::currentDateTimeUtc();
}

void PollWidget::setPoll(const QJsonObject &poll) { m_poll = poll; m_awaitingResults = false; render(); }

void PollWidget::render()
{
    QScrollArea *area = nullptr;
    for (auto *p = parentWidget(); p; p = p->parentWidget()) if ((area = qobject_cast<QScrollArea *>(p))) break;
    const ScrollAnchorGuard keepPosition(area);
    m_expiry.stop(); m_options.clear(); m_vote = nullptr; m_error = nullptr;
    while (auto *item = m_layout->takeAt(0)) { if (auto *w = item->widget()) { w->hide(); w->deleteLater(); } delete item; }
    auto *content = new QWidget; auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(6); m_layout->addWidget(content);
    const auto selected = m_poll.value("viewerOptionId").toString();
    const bool showResults = m_poll.value("ended").toBool() || !selected.isEmpty();
    auto *group = new QButtonGroup(content);
    const qint64 total = std::max<qint64>(0, m_poll.value("totalVotes").toInteger());
    for (const auto &value : m_poll.value("options").toArray()) {
        const auto option = value.toObject(); const auto id = option.value("id").toString();
        if (id.isEmpty()) continue;
        const QString label = option.value("label").toString();
        if (showResults) {
            auto *row = new QWidget; auto *rowLayout = new QVBoxLayout(row); rowLayout->setContentsMargins(0, 0, 0, 0); rowLayout->setSpacing(2);
            const auto votes = std::max<qint64>(0, option.value("voteCount").toInteger());
            const double percent = std::clamp(option.value("percent").toDouble(total > 0 ? 100.0 * votes / total : 0), 0.0, 100.0);
            auto *result = plain(QString("%1%2  ·  %3%  ·  %4票").arg(id == selected ? QStringLiteral("✓ ") : QString(), label, QLocale().toString(percent, 'f', 1), QLocale().toString(votes)), "pollResultLabel");
            result->setProperty("optionId", id); rowLayout->addWidget(result);
            auto *bar = new QProgressBar; bar->setObjectName("pollResult"); bar->setRange(0, 1000); bar->setValue(int(percent * 10)); bar->setTextVisible(false); bar->setFixedHeight(5);
            bar->setStyleSheet("QProgressBar { border:0; border-radius:2px; background:palette(alternate-base); } QProgressBar::chunk { background:palette(highlight); border-radius:2px; }");
            rowLayout->addWidget(bar); layout->addWidget(row);
        } else {
            auto *button = new QRadioButton; button->setAccessibleName(label); button->setObjectName("pollOption"); button->setProperty("optionId", id); button->setCursor(Qt::PointingHandCursor);
            group->addButton(button); layout->addWidget(new PollChoice(button, label)); m_options.append(button);
            button->setChecked(m_choice == id);
            connect(button, &QRadioButton::toggled, this, [this, id](bool checked) { if (checked) { m_choice = id; updateControls(); } });
        }
    }
    if (!showResults && !m_awaitingResults) {
        m_vote = new QPushButton(tr("投票する")); m_vote->setObjectName("pollVote"); m_vote->setCursor(Qt::PointingHandCursor); layout->addWidget(m_vote, 0, Qt::AlignLeft);
        connect(m_vote, &QPushButton::clicked, this, [this] { if (m_api && !m_choice.isEmpty()) { m_error->hide(); m_api->votePoll(m_postId, m_choice); } });
    }
    const auto end = QDateTime::fromString(m_poll.value("endsAt").toString(), Qt::ISODate);
    layout->addWidget(plain(tr("%1票 · %2").arg(QLocale().toString(total), ended() ? tr("終了") : end.toLocalTime().toString("M/d HH:mm") + tr("まで")), "pollStatus"));
    m_error = plain(m_awaitingResults ? tr("結果を更新中…") : QString(), "pollError"); layout->addWidget(m_error); m_error->setVisible(m_awaitingResults);
    if (!ended()) m_expiry.start(int(std::clamp<qint64>(QDateTime::currentDateTimeUtc().msecsTo(end) + 1, 1, 2147483647)));
    updateControls();
}

void PollWidget::updateControls()
{
    const bool allowed = m_interactive && m_api && m_api->allowsScope("polls.vote") && !ended() && m_poll.value("viewerOptionId").toString().isEmpty();
    const bool busy = m_api && m_api->pollBusy(m_postId);
    for (auto *option : m_options) option->setEnabled(allowed && !busy);
    if (m_vote) { m_vote->setEnabled(allowed && !busy && !m_choice.isEmpty()); m_vote->setText(busy ? tr("投票中…") : tr("投票する"));
        m_vote->setToolTip(allowed ? QString() : tr("投票には polls.vote の権限が必要です。OAuthで再ログインするか個人トークンの権限を確認してください")); }
}
