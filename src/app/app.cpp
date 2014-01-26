/*
    Copyright (c) 2013, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "app.h"

#include "common/common.h"
#include "platform/platformnativeinterface.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>
#include <QVariant>
#ifdef HAS_TESTS
#   include <QProcessEnvironment>
#endif

#ifdef Q_OS_UNIX
#   include <signal.h>
#   include <sys/socket.h>
#   include <unistd.h>

namespace {

/**
 * Unix signal handler (TERM, HUP).
 */
void exitSignalHandler(int)
{
    COPYQ_LOG("Terminating application on signal.");
    QCoreApplication::exit();
}

} // namespace

#endif // Q_OS_UNIX

#ifdef Q_OS_WIN
namespace {

void migrateDirectory(const QString oldPath, const QString newPath)
{
    QDir oldDir(oldPath);
    QDir newDir(newPath);

    if ( oldDir.exists() && newDir.exists() ) {
        foreach ( const QString &fileName, oldDir.entryList(QDir::Files) ) {
            const QString oldFileName = oldDir.absoluteFilePath(fileName);
            const QString newFileName = newDir.absoluteFilePath(fileName);
            COPYQ_LOG( QString("Migrating \"%1\" -> \"%2\"")
                       .arg(oldFileName)
                       .arg(newFileName) );
            QFile::copy(oldFileName, newFileName);
        }
    }
}

void migrateConfigToAppDir()
{
    const QString path = QCoreApplication::applicationDirPath() + "/config";
    QDir dir(path);

    if ( (dir.isReadable() || dir.mkdir(".")) && dir.mkpath("copyq") ) {
        QSettings oldSettings;
        const QString oldConfigFileName =
                QSettings(QSettings::IniFormat, QSettings::UserScope,
                          QCoreApplication::organizationName(),
                          QCoreApplication::applicationName()).fileName();
        const QString oldConfigPath = QDir::cleanPath(oldConfigFileName + "/..");

        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings newSettings;

        if ( newSettings.allKeys().isEmpty() ) {
            COPYQ_LOG("Migrating configuration to application directory.");
            const QString newConfigPath = QDir::cleanPath(newSettings.fileName() + "/..");

            // Migrate configuration from system directory.
            migrateDirectory(oldConfigPath, newConfigPath);

            // Migrate themes from system directory.
            QDir(newConfigPath).mkdir("themes");
            migrateDirectory(oldConfigPath + "/themes", newConfigPath + "/themes");

            // Migrate rest of the configuration from the system registry.
            foreach ( const QString &key, oldSettings.allKeys() )
                newSettings.setValue(key, oldSettings.value(key));
        }
    } else {
        COPYQ_LOG( QString("Cannot use \"%1\" directory to save user configuration and items.")
                   .arg(path) );
    }
}

} // namespace
#endif

App::App(QCoreApplication *application, const QString &sessionName)
    : m_app(application)
    , m_exitCode(0)
    , m_closed(false)
{
    QString session("copyq");
    if ( !sessionName.isEmpty() ) {
        session += "-" + sessionName;
        m_app->setProperty( "CopyQ_session_name", QVariant(sessionName) );
    }

#ifdef HAS_TESTS
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if ( env.value("COPYQ_TESTING") == "1" )
        session += ".test";
#endif

    QCoreApplication::setOrganizationName(session);
    QCoreApplication::setApplicationName(session);

#ifdef Q_OS_WIN
    migrateConfigToAppDir();
#endif

    QString locale = QSettings().value("Options/language").toString();
    if (locale.isEmpty())
        locale = QLocale::system().name();

    QTranslator *translator = new QTranslator(m_app.data());
    translator->load("qt_" + locale, QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    QCoreApplication::installTranslator(translator);

    translator = new QTranslator(m_app.data());
    translator->load("copyq_" + locale, ":/translations");
    QCoreApplication::installTranslator(translator);

    QLocale::setDefault(QLocale(locale));

#ifdef Q_OS_UNIX
    // Safely quit application on TERM and HUP signals.
    struct sigaction sigact;

    sigact.sa_handler = ::exitSignalHandler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_flags |= SA_RESTART;

    if ( sigaction(SIGHUP, &sigact, 0) > 0
         || sigaction(SIGINT, &sigact, 0) > 0
         || sigaction(SIGTERM, &sigact, 0) > 0 )
        log( QString("sigaction() failed!"), LogError );
#endif

    createPlatformNativeInterface()->onApplicationStarted();
}

App::~App()
{
}

int App::exec()
{
    if (m_closed) {
        m_app->processEvents();
        return m_exitCode;
    } else {
        return m_app->exec();
    }
}

void App::exit(int exitCode)
{
    QCoreApplication::exit(exitCode);
    m_exitCode = exitCode;
    m_closed = true;
}