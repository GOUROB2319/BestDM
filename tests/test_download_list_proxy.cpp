#include "../BestDM/downloadlistproxy.h"

#include <QAbstractListModel>
#include <QStandardPaths>
#include <QtTest>

class FixtureModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { IdRole = Qt::UserRole + 1, NameRole, StateRole, UrlRole, CreatedRole, SizeRole, ReceivedRole, CategoryRole };
    struct Entry { QString id, name, state, url; qint64 created, size, received; QString category = QStringLiteral("Uncategorized"); };
    QList<Entry> entries;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : entries.size(); }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= entries.size()) return {};
        const auto &entry = entries.at(index.row());
        switch (role) {
        case IdRole: return entry.id;
        case NameRole: return entry.name;
        case StateRole: return entry.state;
        case UrlRole: return entry.url;
        case CreatedRole: return entry.created;
        case SizeRole: return entry.size;
        case ReceivedRole: return entry.received;
        case CategoryRole: return entry.category;
        default: return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override {
        return {{IdRole, "downloadId"}, {NameRole, "fileName"}, {StateRole, "downloadState"},
                {UrlRole, "url"}, {CreatedRole, "createdAt"}, {SizeRole, "total"}, {ReceivedRole, "received"},
                {CategoryRole, "category"}};
    }
};

class AlternateRoleFixtureModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { IdRole = Qt::UserRole + 20, NameRole, StateRole, UrlRole, CreatedRole, SizeRole, ReceivedRole, CategoryRole };
    struct Entry { QString id, name, state, url; qint64 created, size, received; QString category; };
    QList<Entry> entries;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : entries.size(); }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= entries.size()) return {};
        const auto &entry = entries.at(index.row());
        switch (role) {
        case IdRole: return entry.id;
        case NameRole: return entry.name;
        case StateRole: return entry.state;
        case UrlRole: return entry.url;
        case CreatedRole: return entry.created;
        case SizeRole: return entry.size;
        case ReceivedRole: return entry.received;
        case CategoryRole: return entry.category;
        default: return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override {
        return {{IdRole, "downloadId"}, {NameRole, "fileName"}, {StateRole, "downloadState"},
                {UrlRole, "url"}, {CreatedRole, "createdAt"}, {SizeRole, "total"},
                {ReceivedRole, "received"}, {CategoryRole, "category"}};
    }
};

class DownloadListProxyTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("BestDMProxyTests"));
        QCoreApplication::setApplicationName(QStringLiteral("BestDMProxyTests"));
    }

    void filtersRowsInsteadOfHidingDelegates() {
        FixtureModel source;
        source.entries = {
            {QStringLiteral("old"), QStringLiteral("Alpha.zip"), QStringLiteral("Completed"), QStringLiteral("https://a.test/alpha"), 10, 100, 100},
            {QStringLiteral("active"), QStringLiteral("Beta.iso"), QStringLiteral("Downloading"), QStringLiteral("https://b.test/beta"), 20, 200, 50},
            {QStringLiteral("failed"), QStringLiteral("Gamma.log"), QStringLiteral("Failed"), QStringLiteral("https://c.test/gamma"), 30, 300, 300}
        };
        DownloadListProxy proxy;
        proxy.setSourceModel(&source);
        QCOMPARE(proxy.rowCount(), 3);
        proxy.setFilter(QStringLiteral("Active"));
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.data(proxy.index(0, 0), FixtureModel::IdRole).toString(), QStringLiteral("active"));
        proxy.setFilter(QStringLiteral("All"));
        proxy.setSearch(QStringLiteral("GAMMA"));
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.data(proxy.index(0, 0), FixtureModel::IdRole).toString(), QStringLiteral("failed"));
    }

    void sortsByNewestByDefaultAndByNameOnRequest() {
        FixtureModel source;
        source.entries = {
            {QStringLiteral("z"), QStringLiteral("zeta.bin"), QStringLiteral("Completed"), QString{}, 20, 100, 100},
            {QStringLiteral("a"), QStringLiteral("Alpha.bin"), QStringLiteral("Completed"), QString{}, 10, 200, 200}
        };
        DownloadListProxy proxy;
        proxy.setSourceModel(&source);
        proxy.setSorting(QStringLiteral("Date"));
        QCOMPARE(proxy.data(proxy.index(0, 0), FixtureModel::IdRole).toString(), QStringLiteral("z"));
        proxy.setSorting(QStringLiteral("Name"));
        QCOMPARE(proxy.data(proxy.index(0, 0), FixtureModel::IdRole).toString(), QStringLiteral("a"));
        QCOMPARE(proxy.data(proxy.index(1, 0), FixtureModel::IdRole).toString(), QStringLiteral("z"));
    }

    void filtersByCategoryWithoutChangingTaskData() {
        FixtureModel source;
        source.entries = {
            {QStringLiteral("archive"), QStringLiteral("one.zip"), QStringLiteral("Completed"), QString{}, 10, 100, 100},
            {QStringLiteral("image"), QStringLiteral("two.png"), QStringLiteral("Paused"), QString{}, 20, 200, 50}
        };
        source.entries[0].category = QStringLiteral("Archives");
        source.entries[1].category = QStringLiteral("Images");
        DownloadListProxy proxy;
        proxy.setSourceModel(&source);
        proxy.setCategoryFilter(QStringLiteral("Archives"));
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.data(proxy.index(0, 0), FixtureModel::IdRole).toString(), QStringLiteral("archive"));
        QCOMPARE(source.entries[1].state, QStringLiteral("Paused"));
        proxy.setCategoryFilter(QStringLiteral("All categories"));
        QCOMPARE(proxy.rowCount(), 2);
    }

    void rebuildsRoleCacheWhenSourceModelChanges() {
        FixtureModel first;
        first.entries = {{QStringLiteral("first"), QStringLiteral("first.bin"), QStringLiteral("Failed"),
                          QStringLiteral("https://first.test"), 10, 100, 0}};
        AlternateRoleFixtureModel second;
        second.entries = {
            {QStringLiteral("skip"), QStringLiteral("other.bin"), QStringLiteral("Completed"), QString{}, 20, 100, 100, QStringLiteral("Other")},
            {QStringLiteral("keep"), QStringLiteral("wanted.bin"), QStringLiteral("Failed"), QString{}, 30, 200, 50, QStringLiteral("Wanted")}
        };
        DownloadListProxy proxy;
        proxy.setSourceModel(&first);
        proxy.setFilter(QStringLiteral("Failed"));
        proxy.setSourceModel(&second);
        proxy.setCategoryFilter(QStringLiteral("Wanted"));
        proxy.setSearch(QStringLiteral("wanted"));
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.data(proxy.index(0, 0), AlternateRoleFixtureModel::IdRole).toString(), QStringLiteral("keep"));
    }
};

QTEST_GUILESS_MAIN(DownloadListProxyTest)
#include "test_download_list_proxy.moc"
