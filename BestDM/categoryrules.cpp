#include "categoryrules.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <utility>

bool CategoryRules::setText(const QString &text, QString *error) {
    QList<Rule> parsedRules;
    QStringList normalizedLines;
    static const QRegularExpression rulePattern(QStringLiteral("^(ext|host)\\s*:\\s*([^=]+?)\\s*=\\s*(.+)$"),
                                                QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression extensionPattern(QStringLiteral("^[a-z0-9][a-z0-9_-]{0,15}$"),
                                                     QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression categoryPattern(QStringLiteral("^[^\\r\\n=\\x00-\\x1f]{1,40}$"));
    const auto lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    if (lines.size() > 100) {
        if (error) *error = QStringLiteral("Use no more than 100 category rules.");
        return false;
    }
    for (const auto &line : lines) {
        const auto match = rulePattern.match(line.trimmed());
        if (!match.hasMatch()) {
            if (error) *error = QStringLiteral("Use one rule per line: ext: zip = Archives or host: example.com = Vendor.");
            return false;
        }
        const QString type = match.captured(1).toLower();
        QString matcher = match.captured(2).trimmed().toLower();
        if (type == QStringLiteral("ext") && matcher.startsWith(QLatin1Char('.'))) matcher.remove(0, 1);
        const QString category = match.captured(3).trimmed();
        bool validMatcher = extensionPattern.match(matcher).hasMatch();
        if (type == QStringLiteral("host")) {
            const QUrl hostUrl(QStringLiteral("http://") + matcher, QUrl::StrictMode);
            validMatcher = hostUrl.isValid() && !hostUrl.host().isEmpty()
                && hostUrl.host().compare(matcher, Qt::CaseInsensitive) == 0 && hostUrl.port(-1) == -1;
        }
        if (!validMatcher || !categoryPattern.match(category).hasMatch()) {
            if (error) *error = QStringLiteral("A category rule has an invalid extension, host, or category name.");
            return false;
        }
        parsedRules.append({type, matcher, category});
        normalizedLines.append(QStringLiteral("%1: %2 = %3").arg(type, matcher, category));
    }
    m_text = normalizedLines.join(QLatin1Char('\n'));
    m_rules = std::move(parsedRules);
    if (error) error->clear();
    return true;
}

QString CategoryRules::categoryFor(const QUrl &url, const QString &fileName) const {
    const QString extension = QFileInfo(fileName).suffix().toLower();
    const QString host = url.host().toLower();
    for (const auto &rule : m_rules) {
        if (rule.type == QStringLiteral("ext") && extension == rule.matcher) return rule.category;
        if (rule.type == QStringLiteral("host")
            && (host == rule.matcher || host.endsWith(QStringLiteral(".") + rule.matcher))) return rule.category;
    }
    return QStringLiteral("Uncategorized");
}

QString CategoryRules::safeFolderName(const QString &category) {
    QString segment = category;
    segment.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")), QStringLiteral("_"));
    segment = segment.trimmed();
    while (segment.endsWith(QLatin1Char('.')) || segment.endsWith(QLatin1Char(' '))) segment.chop(1);
    if (segment.isEmpty() || segment == QStringLiteral(".") || segment == QStringLiteral(".."))
        return QStringLiteral("Uncategorized");
    static const QRegularExpression reservedFolder(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$"),
                                                   QRegularExpression::CaseInsensitiveOption);
    if (reservedFolder.match(segment).hasMatch()) segment.prepend(QLatin1Char('_'));
    return segment;
}
