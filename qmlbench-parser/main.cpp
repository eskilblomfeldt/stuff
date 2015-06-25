#include <QtCore>

struct BenchmarkData
{
    BenchmarkData()
        : average(0.0)
    {}

    qreal average;
    QStringList results;
    QDateTime time;

    QString baseCommit;
    QString declarativeCommit;
    QString renderer;
    QString vendor;
    QString driverVersion;

    QString platformPlugin;
    QString productName;

    QString windowSize;
};

typedef QPair<BenchmarkData, BenchmarkData> BenchmarkDataPair;

void collectData(const QFileInfo &fileInfo, QHash<QString, BenchmarkDataPair> *benchmarkDatas)
{    
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        fprintf(stderr, "Cannot open file for reading: %s\n", qPrintable(fileInfo.absoluteFilePath()));
        return;
    }

    QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = document.object();

    QString baseCommit;
    QString declarativeCommit;
    QString id = root.value(QStringLiteral("id")).toString();
    QStringList commits = id.split(QLatin1Char(','));
    if (commits.size() != 2) {
        fprintf(stderr, "Warning: Misformed id in json file %s: %s\n", qPrintable(fileInfo.absoluteFilePath()), qPrintable(id));
    } else {
        baseCommit = commits.at(0);
        declarativeCommit = commits.at(1);
    }

    QString windowSize = root.value(QStringLiteral("windowSize")).toString();
    QString renderer;
    QString vendor;
    QString driverVersion;
    {
        QJsonObject opengl = root.value(QStringLiteral("opengl")).toObject();
        renderer = opengl.value(QStringLiteral("renderer")).toString();
        vendor = opengl.value(QStringLiteral("vendor")).toString();
        driverVersion = opengl.value(QStringLiteral("version")).toString();
    }

    QString platformPlugin;
    QString productName;
    {
        QJsonObject os = root.value(QStringLiteral("os")).toObject();
        platformPlugin = os.value(QStringLiteral("platformPlugin")).toString();
        productName = os.value(QStringLiteral("prettyProductName")).toString();
    }

    QJsonObject::const_iterator it;
    for (it = root.constBegin(); it != root.constEnd(); ++it) {
        QString key = it.key();
        if (key != QStringLiteral("os") && key != QStringLiteral("opengl") && key != QStringLiteral("windowSize") && QFileInfo(key).exists()) {
            BenchmarkDataPair &dataPair = (*benchmarkDatas)[key];
            if (!dataPair.first.time.isValid() || !dataPair.second.time.isValid()) {
                BenchmarkData &data = dataPair.first.time.isValid() ? dataPair.second : dataPair.first;
                data.time = QFileInfo(file).lastModified();
                data.windowSize = windowSize;
                data.renderer = renderer;
                data.vendor = vendor;
                data.driverVersion = driverVersion;
                data.platformPlugin = platformPlugin;
                data.productName = productName;
                data.baseCommit = baseCommit;
                data.declarativeCommit = declarativeCommit;

                QJsonObject o = it.value().toObject();
                data.average = o.value(QStringLiteral("average")).toDouble();

                QJsonArray array = o.value(QStringLiteral("results")).toArray();
                for (int j = 0; j < array.size(); ++j)
                    data.results.append(QString::number(array.at(j).toDouble()));
            }
        }
    }
}

QHash<QString, BenchmarkDataPair> collectData(const QString &directory)
{
    QList<QFileInfo> entries = QDir(directory).entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);
    QHash<QString, BenchmarkDataPair> ret;
    foreach (QFileInfo entry, entries)
        collectData(entry, &ret);

    return ret;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QStringList arguments = app.arguments();

    QString smtpServer = QStringLiteral("localhost");
    QString senderEmail = QStringLiteral("nobody@nowhere");
    qreal errorMargin = 0.01;
    QString directory;
    QString email;

    bool helpRequested = false;
    for (int i = 1; i < arguments.size() && !helpRequested; ++i) {
        QString argument = arguments.at(i);
        if (argument == QStringLiteral("-s")) {
            if (++i < arguments.size()) {
                smtpServer = arguments.at(i);
            } else {
                helpRequested = true;
            }
        } else if (argument == QStringLiteral("-f")) {
            if (++i < arguments.size()) {
                senderEmail = arguments.at(i);
            } else {
                helpRequested = true;
            }
        } else if (argument == QStringLiteral("-e")) {
            if (++i < arguments.size()) {
                errorMargin = arguments.at(i).toDouble(&helpRequested);
            } else {
                helpRequested = true;
            }
        } else if (argument == QStringLiteral("-h")) {
            helpRequested = true;
        } else if (directory.isEmpty()) {
            directory = argument;
        } else if (email.isEmpty()) {
            email = argument;
        } else {
            helpRequested = true;
        }
    }

    if (directory.isEmpty() || email.isEmpty())
        helpRequested = true;

    if (helpRequested) {
        // 01234567890123456789012345678901234567890123456789012345678901234567890123456789
        fprintf(stderr, "Usage: %s <directory> <recipient e-mail> [options]\n\n", argv[0]);
        fprintf(stderr, "Parses .json files in specified directory for qmlbench output and compares the\n"
                        "most recent results to the previous one for the same benchmark. If the results\n"
                        "differ significantly, a mail will be sent to the specified address.\n\n"
                        "Options:\n"
                        "   -s <smtp-server>    Default: localhost)\n"
                        "   -f <sender e-mail>  Default: nobody@nowhere)\n"
                        "   -e <error margin>   Default: 0.03)\n"
                        "   -b <branch>         Qt branch being tested\n"
                        "   -h                  Show this message)\n");
        return 1;
    }

    QHash<QString, BenchmarkDataPair> benchmarkDatas = collectData(directory);

    QString testResultsFormatted;
    QHash<QString, BenchmarkDataPair>::const_iterator it;
    for (it = benchmarkDatas.constBegin(); it != benchmarkDatas.constEnd(); ++it) {
        const BenchmarkDataPair &dataPair = it.value();
        if (dataPair.first.time.isValid() && dataPair.second.time.isValid()) {
            qreal difference = qAbs((dataPair.second.average - dataPair.first.average) / dataPair.first.average);
            if (qAbs(difference) >= errorMargin) {
                if (difference < 0.0)
                    testResultsFormatted += QStringLiteral("\n\n____IMPROVEMENT DETECTED____\n");
                else
                    testResultsFormatted += QStringLiteral("\n\n____REGRESSION DETECTED_____\n");

                testResultsFormatted += QStringLiteral("    Name: %1").arg(it.key());
                testResultsFormatted += QStringLiteral("        Previous data point: qtbase=%1, qtdeclarative=%2 (%3)\n")
                        .arg(dataPair.first.baseCommit).arg(dataPair.first.declarativeCommit).arg(dataPair.first.time.toString(Qt::ISODate));
                testResultsFormatted += QStringLiteral("        Current data point : qtbase=%1, qtdeclarative=%2 (%3)\n")
                        .arg(dataPair.second.baseCommit).arg(dataPair.second.declarativeCommit).arg(dataPair.second.time.toString(Qt::ISODate));
                testResultsFormatted += QStringLiteral("        Average: %1 (was: %2, change: %3%)\n").arg(dataPair.second.average).arg(dataPair.first.average).arg(difference * 100.0);
                testResultsFormatted += QStringLiteral("        Results: %1 (was: %2)\n").arg(dataPair.second.results.join(QLatin1Char(','))).arg(dataPair.first.results.join(QLatin1Char(',')));
                testResultsFormatted += QStringLiteral("        Window size: %1 %2\n")
                        .arg(dataPair.second.windowSize)
                        .arg(dataPair.second.windowSize != dataPair.first.windowSize ? QStringLiteral("(was: %1").arg(dataPair.first.windowSize) : QString());
                testResultsFormatted += QStringLiteral("        Renderer: %1 %2\n")
                        .arg(dataPair.second.renderer)
                        .arg(dataPair.second.renderer != dataPair.first.renderer ? QStringLiteral("(was: %1").arg(dataPair.first.renderer) : QString());
                testResultsFormatted += QStringLiteral("        Vendor: %1 %2\n")
                        .arg(dataPair.second.vendor)
                        .arg(dataPair.second.vendor != dataPair.first.vendor ? QStringLiteral("(was: %1").arg(dataPair.first.vendor) : QString());
                testResultsFormatted += QStringLiteral("        Driver version: %1 %2\n")
                        .arg(dataPair.second.driverVersion)
                        .arg(dataPair.second.driverVersion != dataPair.first.driverVersion ? QStringLiteral("(was: %1").arg(dataPair.first.driverVersion) : QString());
                testResultsFormatted += QStringLiteral("        Platform plugin: %1 %2\n")
                        .arg(dataPair.second.platformPlugin)
                        .arg(dataPair.second.platformPlugin != dataPair.first.platformPlugin ? QStringLiteral("(was: %1").arg(dataPair.first.platformPlugin) : QString());
                testResultsFormatted += QStringLiteral("        OS: %1 %2\n")
                        .arg(dataPair.second.productName)
                        .arg(dataPair.second.productName != dataPair.first.productName ? QStringLiteral("(was: %1").arg(dataPair.first.productName) : QString());
            }
        }
    }

    if (!testResultsFormatted.isEmpty()) {
        fprintf(stdout, "Reporting to %s\n", qPrintable(email));
        testResultsFormatted = QStringLiteral("Discrepancies detected when running benchmarks today in %1").arg(QFileInfo(directory).baseName()) + testResultsFormatted + QStringLiteral("\n\nHave a nice day!");

        QProcess process;
        process.setProgram(QStringLiteral("sendemail"));
        process.setArguments(QStringList()
                                << QStringLiteral("-s")
                                << smtpServer
                                << QStringLiteral("-f")
                                << senderEmail
                                << QStringLiteral("-u")
                                << QStringLiteral("[Lancelot QmlBench] Discrepancies in benchmarks")
                                << QStringLiteral("-t")
                                << email
                                << QStringLiteral("-m")
                                << testResultsFormatted);
        process.start();
        if (!process.waitForFinished()) {
            fprintf(stderr, "sendemail failed\n");
            return 2;
        }
    } else {
        fprintf(stdout, "Nothing to report\n");
    }

    return 0;
}
