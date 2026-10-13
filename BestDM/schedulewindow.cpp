#include "schedulewindow.h"

QString ScheduleWindow::dayName(int dayNumber) {
    switch (dayNumber) {
    case 1: return QStringLiteral("Mon");
    case 2: return QStringLiteral("Tue");
    case 3: return QStringLiteral("Wed");
    case 4: return QStringLiteral("Thu");
    case 5: return QStringLiteral("Fri");
    case 6: return QStringLiteral("Sat");
    case 7: return QStringLiteral("Sun");
    default: return {};
    }
}

int ScheduleWindow::dayNumber(const QString &name) {
    for (int day = 1; day <= 7; ++day)
        if (dayName(day).compare(name.trimmed(), Qt::CaseInsensitive) == 0) return day;
    return 0;
}

bool ScheduleWindow::allows(const QDateTime &localTime) const {
    if (!enabled) return true;
    if (!localTime.isValid() || daysOfWeek.isEmpty() || !startTime.isValid() || !endTime.isValid()) return false;
    const int today = localTime.date().dayOfWeek();
    const QTime time = localTime.time();
    if (startTime == endTime) return daysOfWeek.contains(today);
    if (startTime < endTime)
        return daysOfWeek.contains(today) && time >= startTime && time < endTime;
    if (time >= startTime) return daysOfWeek.contains(today);
    if (time < endTime) return daysOfWeek.contains(today == 1 ? 7 : today - 1);
    return false;
}
