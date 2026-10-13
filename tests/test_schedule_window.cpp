#include "../BestDM/schedulewindow.h"

#include <QtTest>

class ScheduleWindowTest final : public QObject {
    Q_OBJECT
private slots:
    void disabledScheduleAlwaysAllows() {
        ScheduleWindow schedule;
        QVERIFY(schedule.allows(QDateTime(QDate(2026, 10, 12), QTime(3, 0))));
    }

    void enforcesSelectedDaysAndHalfOpenTimes() {
        ScheduleWindow schedule;
        schedule.enabled = true;
        schedule.daysOfWeek = QSet<int>{1, 3, 5};
        schedule.startTime = QTime(9, 0);
        schedule.endTime = QTime(17, 0);
        QVERIFY(schedule.allows(QDateTime(QDate(2026, 10, 12), QTime(9, 0)))); // Monday
        QVERIFY(!schedule.allows(QDateTime(QDate(2026, 10, 12), QTime(17, 0))));
        QVERIFY(!schedule.allows(QDateTime(QDate(2026, 10, 13), QTime(10, 0)))); // Tuesday
    }

    void overnightWindowUsesStartDayForSelection() {
        ScheduleWindow schedule;
        schedule.enabled = true;
        schedule.daysOfWeek = QSet<int>{1}; // Monday 22:00 through Tuesday 06:00
        schedule.startTime = QTime(22, 0);
        schedule.endTime = QTime(6, 0);
        QVERIFY(schedule.allows(QDateTime(QDate(2026, 10, 12), QTime(23, 0))));
        QVERIFY(schedule.allows(QDateTime(QDate(2026, 10, 13), QTime(5, 59))));
        QVERIFY(!schedule.allows(QDateTime(QDate(2026, 10, 13), QTime(6, 0))));
        QVERIFY(!schedule.allows(QDateTime(QDate(2026, 10, 13), QTime(23, 0))));
    }

    void equalStartAndEndMeansFullSelectedDay() {
        ScheduleWindow schedule;
        schedule.enabled = true;
        schedule.daysOfWeek = QSet<int>{7};
        schedule.startTime = QTime(0, 0);
        schedule.endTime = QTime(0, 0);
        QVERIFY(schedule.allows(QDateTime(QDate(2026, 10, 18), QTime(12, 0))));
        QVERIFY(!schedule.allows(QDateTime(QDate(2026, 10, 19), QTime(12, 0))));
    }
};

QTEST_GUILESS_MAIN(ScheduleWindowTest)
#include "test_schedule_window.moc"
