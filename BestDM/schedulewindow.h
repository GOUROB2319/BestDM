#pragma once

#include <QDateTime>
#include <QSet>
#include <QString>
#include <QTime>

struct ScheduleWindow final {
    bool enabled = false;
    QSet<int> daysOfWeek;
    QTime startTime{0, 0};
    QTime endTime{23, 59};

    bool allows(const QDateTime &localTime) const;
    static int dayNumber(const QString &name);
    static QString dayName(int dayNumber);
};
