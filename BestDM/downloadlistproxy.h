#pragma once

#include <QSortFilterProxyModel>
#include <QHash>
#include <QByteArray>

class DownloadListProxy final : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY viewChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY viewChanged)
    Q_PROPERTY(QString sorting READ sorting WRITE setSorting NOTIFY viewChanged)
    Q_PROPERTY(QString categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY viewChanged)
    Q_PROPERTY(QString queueFilter READ queueFilter WRITE setQueueFilter NOTIFY viewChanged)
public:
    explicit DownloadListProxy(QObject *parent = nullptr);
    void setSourceModel(QAbstractItemModel *sourceModel) override;
    QString filter() const { return m_filter; }
    QString search() const { return m_search; }
    QString sorting() const { return m_sorting; }
    QString categoryFilter() const { return m_categoryFilter; }
    QString queueFilter() const { return m_queueFilter; }
    Q_INVOKABLE void setFilter(const QString &filter);
    Q_INVOKABLE void setSearch(const QString &search);
    Q_INVOKABLE void setSorting(const QString &sorting);
    Q_INVOKABLE void setCategoryFilter(const QString &category);
    Q_INVOKABLE void setQueueFilter(const QString &queueName);
signals:
    void viewChanged();
protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
private:
    QVariant field(const QModelIndex &index, const char *roleName) const;
    QString m_filter = QStringLiteral("All");
    QString m_search;
    QString m_sorting = QStringLiteral("Date");
    QString m_categoryFilter;
    QString m_queueFilter;
    QHash<QByteArray, int> m_roleIds;
};
