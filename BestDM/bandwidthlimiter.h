#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QtGlobal>

// A shared token bucket used by every active download in a queue.
class BandwidthLimiter final {
public:
    void setLimit(qint64 bytesPerSecond);
    qint64 limit() const { return m_limit; }
    void addConsumer(const void *consumer);
    void removeConsumer(const void *consumer);
    qint64 takeAvailable(const void *consumer, qint64 maximumBytes);
    qint64 bufferSizeFor(const void *consumer) const;

private:
    void refill();
    void rebalance(bool resetTokens = false);
    qint64 m_limit = 0; // zero means unlimited
    QHash<const void *, double> m_tokens;
    QElapsedTimer m_clock;
};
