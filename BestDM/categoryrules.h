#pragma once

#include <QList>
#include <QString>
#include <QUrl>

class CategoryRules final {
public:
    QString text() const { return m_text; }
    bool setText(const QString &text, QString *error = nullptr);
    QString categoryFor(const QUrl &url, const QString &fileName) const;
    static QString safeFolderName(const QString &category);

private:
    struct Rule { QString type; QString matcher; QString category; };
    QString m_text;
    QList<Rule> m_rules;
};
