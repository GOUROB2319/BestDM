#include "../BestDM/bandwidthlimiter.h"

#include <QtTest>

class BandwidthLimiterTests final : public QObject {
    Q_OBJECT
private slots:
    void zeroMeansUnlimited() {
        BandwidthLimiter limiter;
        const int consumer = 0;
        limiter.addConsumer(&consumer);
        QCOMPARE(limiter.takeAvailable(&consumer, 12345), qint64(12345));
    }

    void configuredBudgetIsSharedAndBounded() {
        BandwidthLimiter limiter;
        limiter.setLimit(100000);
        const int consumer = 0;
        limiter.addConsumer(&consumer);
        const qint64 initialGrant = limiter.takeAvailable(&consumer, 500000);
        QVERIFY(initialGrant > 0);
        QVERIFY(initialGrant <= 10000);
        QCOMPARE(limiter.limit(), qint64(100000));
    }

    void changingLimitResetsTheTokenWindow() {
        BandwidthLimiter limiter;
        limiter.setLimit(100000);
        const int consumer = 0;
        limiter.addConsumer(&consumer);
        limiter.takeAvailable(&consumer, 10000);
        limiter.setLimit(20000);
        QVERIFY(limiter.takeAvailable(&consumer, 500000) <= 2000);
    }

    void concurrentConsumersReceiveSeparateShares() {
        BandwidthLimiter limiter;
        limiter.setLimit(100000);
        const int first = 1;
        const int second = 2;
        limiter.addConsumer(&first);
        limiter.addConsumer(&second);
        const qint64 firstGrant = limiter.takeAvailable(&first, 100000);
        const qint64 secondGrant = limiter.takeAvailable(&second, 100000);
        QVERIFY(firstGrant > 0);
        QVERIFY(secondGrant > 0);
        QVERIFY(firstGrant + secondGrant <= 10000);
    }
};

QTEST_GUILESS_MAIN(BandwidthLimiterTests)
#include "test_bandwidth_limiter.moc"
