

#include "nextcloudtheme.h"

#include <QString>
#include <QVariant>
#ifndef TOKEN_AUTH_ONLY
#include <QPixmap>
#include <QIcon>
#endif
#include <QCoreApplication>

#include "config.h"
#include "common/utility.h"
#include "version.h"

namespace OCC {

NextcloudTheme::NextcloudTheme()
    : Theme()
{
}

QString NextcloudTheme::wizardUrlHint() const
{
    return QStringLiteral("Please insert your server address here");
}

}
