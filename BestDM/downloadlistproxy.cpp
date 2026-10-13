#include "downloadlistproxy.h"

#include <QSettings>

DownloadListProxy::DownloadListProxy(QObject *parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    const QString saved = QSettings().value(QStringLiteral("downloads/sort"), QStringLiteral("Date")).toString();
    m_sorting.clear();
    setSorting(saved);
    if (m_sorting.isEmpty()) setSorting(QStringLiteral("Date"));
}

void DownloadListProxy::setSourceModel(QAbstractItemModel *sourceModel) {
    m_roleIds.clear();
    if (sourceModel) {
        const auto names = sourceModel->roleNames();
        for (auto it = names.cbegin(); it != names.cend(); ++it)
            m_roleIds.insert(it.value(), it.key());
    }
    QSortFilterProxyModel::setSourceModel(sourceModel);
}

void DownloadListProxy::setFilter(const QString &filter) {
    if (filter != QStringLiteral("All") && filter != QStringLiteral("Active")
        && filter != QStringLiteral("Queued") && filter != QStringLiteral("Paused")
        && filter != QStringLiteral("Completed") && filter != QStringLiteral("Failed")
        && filter != QStringLiteral("History")) return;
    if (m_filter == filter) return;
    m_filter = filter;
    invalidateFilter();
    emit viewChanged();
}

void DownloadListProxy::setSearch(const QString &search) {
    const QString normalized = search.trimmed();
    if (m_search == normalized) return;
    m_search = normalized;
    invalidateFilter();
    emit viewChanged();
}

void DownloadListProxy::setSorting(const QString &sorting) {
    if (sorting != QStringLiteral("Date") && sorting != QStringLiteral("Name")
        && sorting != QStringLiteral("State") && sorting != QStringLiteral("Size")
        && sorting != QStringLiteral("Progress")) return;
    if (m_sorting == sorting) return;
    m_sorting = sorting;
    QSettings().setValue(QStringLiteral("downloads/sort"), sorting);
    sort(0, sorting == QStringLiteral("Date") || sorting == QStringLiteral("Size")
             || sorting == QStringLiteral("Progress") ? Qt::DescendingOrder : Qt::AscendingOrder);
    emit viewChanged();
}

void DownloadListProxy::setCategoryFilter(const QString &category) {
    const QString normalized = category == QStringLiteral("All categories") ? QString{} : category;
    if (m_categoryFilter == normalized) return;
    m_categoryFilter = normalized;
    invalidateFilter();
    emit viewChanged();
}

void DownloadListProxy::setQueueFilter(const QString &queueName) {
    const QString normalized = queueName.trimmed();
    if (m_queueFilter == normalized) return;
    m_queueFilter = normalized;
    invalidateFilter();
    emit viewChanged();
}

QVariant DownloadListProxy::field(const QModelIndex &index, const char *roleName) const {
    if (!sourceModel()) return {};
    const auto role = m_roleIds.constFind(QByteArray(roleName));
    return role == m_roleIds.cend() ? QVariant{} : sourceModel()->data(index, role.value());
}

bool DownloadListProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    const auto model = sourceModel();
    if (!model) return false;
    const auto index = model->index(sourceRow, 0, sourceParent);
    if (!m_queueFilter.isEmpty()
        && field(index, "queueName").toString().compare(m_queueFilter, Qt::CaseInsensitive) != 0) return false;
    if (!m_categoryFilter.isEmpty() && field(index, "category").toString() != m_categoryFilter) return false;
    const QString state = field(index, "downloadState").toString();
    if (m_filter == QStringLiteral("Active") && state != QStringLiteral("Downloading")
        && state != QStringLiteral("Verifying") && state != QStringLiteral("Probing")) return false;
    if (m_filter == QStringLiteral("Queued") && state != QStringLiteral("Queued")
        && state != QStringLiteral("Waiting for schedule") && state != QStringLiteral("Retrying")) return false;
    if (m_filter == QStringLiteral("Paused") && state != QStringLiteral("Paused")) return false;
    if (m_filter == QStringLiteral("Completed") && state != QStringLiteral("Completed")) return false;
    if (m_filter == QStringLiteral("Failed") && state != QStringLiteral("Failed")) return false;
    if (m_filter == QStringLiteral("History") && state != QStringLiteral("Completed")
        && state != QStringLiteral("Failed") && state != QStringLiteral("Canceled")) return false;
    if (m_search.isEmpty()) return true;
    return field(index, "fileName").toString().contains(m_search, Qt::CaseInsensitive)
        || field(index, "url").toString().contains(m_search, Qt::CaseInsensitive);
}

bool DownloadListProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    const auto leftField = [&](const char *role) { return field(left, role); };
    const auto rightField = [&](const char *role) { return field(right, role); };
    int comparison = 0;
    if (m_sorting == QStringLiteral("Name"))
        comparison = QString::compare(leftField("fileName").toString(), rightField("fileName").toString(), Qt::CaseInsensitive);
    else if (m_sorting == QStringLiteral("State"))
        comparison = QString::compare(leftField("downloadState").toString(), rightField("downloadState").toString(), Qt::CaseInsensitive);
    else if (m_sorting == QStringLiteral("Size"))
        comparison = leftField("total").toLongLong() < rightField("total").toLongLong() ? -1
            : (leftField("total").toLongLong() > rightField("total").toLongLong() ? 1 : 0);
    else if (m_sorting == QStringLiteral("Progress")) {
        const auto ratio = [](const QModelIndex &index, const DownloadListProxy *self) {
            const qint64 total = self->field(index, "total").toLongLong();
            return total > 0 ? double(self->field(index, "received").toLongLong()) / double(total) : -1.0;
        };
        const double a = ratio(left, this), b = ratio(right, this);
        comparison = a < b ? -1 : (a > b ? 1 : 0);
    } else {
        const qint64 a = leftField("createdAt").toLongLong(), b = rightField("createdAt").toLongLong();
        comparison = a < b ? -1 : (a > b ? 1 : 0);
    }
    if (comparison == 0)
        comparison = QString::compare(leftField("downloadId").toString(), rightField("downloadId").toString(), Qt::CaseSensitive);
    return comparison < 0;
}
