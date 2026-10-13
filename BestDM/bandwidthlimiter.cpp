#include "bandwidthlimiter.h"

#include <algorithm>

void BandwidthLimiter::setLimit(qint64 bytesPerSecond) {
    m_limit = std::max<qint64>(0, bytesPerSecond);
    m_clock.start();
    rebalance(true);
}

void BandwidthLimiter::addConsumer(const void *consumer) {
    if (!consumer || m_tokens.contains(consumer)) return;
    refill();
    m_tokens.insert(consumer, 0.0);
    if (m_limit > 0) {
        const double share = double(m_limit) / double(m_tokens.size());
        m_tokens[consumer] = std::max(1.0, std::min(65'536.0, share * 0.1));
    }
    rebalance();
}

void BandwidthLimiter::removeConsumer(const void *consumer) {
    if (!m_tokens.contains(consumer)) return;
    refill();
    m_tokens.remove(consumer);
    rebalance();
}

qint64 BandwidthLimiter::takeAvailable(const void *consumer, qint64 maximumBytes) {
    if (maximumBytes <= 0) return 0;
    if (m_limit == 0) return maximumBytes;
    if (!m_tokens.contains(consumer)) return 0;
    refill();
    auto &tokens = m_tokens[consumer];
    const qint64 granted = std::min(maximumBytes, qint64(tokens));
    tokens -= double(granted);
    return granted;
}

qint64 BandwidthLimiter::bufferSizeFor(const void *consumer) const {
    if (m_limit == 0 || !m_tokens.contains(consumer)) return 256 * 1024;
    const double share = double(m_limit) / double(m_tokens.size());
    return std::max<qint64>(1, qint64(std::min(65'536.0, share * 0.1)));
}

void BandwidthLimiter::refill() {
    if (!m_clock.isValid()) m_clock.start();
    const qint64 elapsedMs = m_clock.restart();
    if (m_limit == 0 || m_tokens.isEmpty() || elapsedMs <= 0) return;
    const double share = double(m_limit) / double(m_tokens.size());
    const double capacity = std::max(1.0, std::min(65'536.0, share * 0.1));
    const double refillBytes = share * double(elapsedMs) / 1000.0;
    for (auto it = m_tokens.begin(); it != m_tokens.end(); ++it)
        it.value() = std::min(capacity, it.value() + refillBytes);
}

void BandwidthLimiter::rebalance(bool resetTokens) {
    if (m_limit <= 0 || m_tokens.isEmpty()) return;
    const double share = double(m_limit) / double(m_tokens.size());
    const double capacity = std::max(1.0, std::min(65'536.0, share * 0.1));
    for (auto it = m_tokens.begin(); it != m_tokens.end(); ++it) {
        if (resetTokens || it.value() > capacity) it.value() = capacity;
    }
}
