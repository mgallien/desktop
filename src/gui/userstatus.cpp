/*
 * Copyright (C) by Camila <hello@camila.codes>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "userstatus.h"
#include "account.h"
#include "accountstate.h"
#include "networkjobs.h"
#include "folderman.h"
#include "creds/abstractcredentials.h"
#include "theme.h"

#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>

namespace OCC {

Q_LOGGING_CATEGORY(lcUserStatus, "nextcloud.gui.userstatus", QtInfoMsg)

UserStatus::UserStatus(QObject *parent)
    : QObject(parent)
{
}

UserStatus::Status UserStatus::stringToEnum(const QString &status) const 
{
    // it needs to match the Status enum
    const QHash<QString, Status> preDefinedStatus{{"online", Online},
                                               {"dnd", DoNotDisturb}, //DoNotDisturb
                                               {"away", Away},
                                               {"offline", Offline},
                                               {"invisible", Invisible}};
    
    // api should return invisible, dnd,... toLower() it is to make sure 
    // it matches _preDefinedStatus, otherwise the default is online (0)
    const auto statusKey = status.isEmpty() ? QStringLiteral("online") : status.toLower();
    return preDefinedStatus.value(statusKey, Online);
}

QString UserStatus::enumToUserString(Status status) const 
{
    switch (status) {
    case Away:
        return tr("Away");
    case DoNotDisturb:
        return tr("Do not disturb");
    case Invisible:
    case Offline:
        return tr("Offline");
    default:
        return tr("Online");
    }
}

void UserStatus::fetchUserStatus(AccountPtr account)
{
    if (_job) {
        _job->deleteLater();
    }

    _job = new JsonApiJob(account, QStringLiteral("/ocs/v2.php/apps/user_status/api/v1/user_status"), this);
    connect(_job.data(), &JsonApiJob::jsonReceived, this, &UserStatus::slotFetchUserStatusFinished);
    _job->start();
}

void UserStatus::slotFetchUserStatusFinished(const QJsonDocument &json, int statusCode)
{
    const QJsonObject defaultValues {
        {"icon", ""},
        {"message", ""},
        {"status", "online"}
    };
    
    if (statusCode != 200) {
        qCInfo(lcUserStatus) << "Slot fetch UserStatus finished with status code" << statusCode;
        qCInfo(lcUserStatus) << "Using then default values as if user has not set any status" << defaultValues;
    }
    
    const auto retrievedData = json.object().value("ocs").toObject().value("data").toObject(defaultValues);
    const auto emoji = retrievedData.value("icon").toString();
    const auto message = retrievedData.value("message").toString();
    auto statusString = retrievedData.value("status").toString(); 

    _status = stringToEnum(statusString);
    statusString = enumToUserString(_status);
    const auto visibleStatusText = message.isEmpty() ? statusString : message;

    _message = QString("%1 %2").arg(emoji, visibleStatusText);
    emit fetchUserStatusFinished();
}

UserStatus::Status UserStatus::status() const
{
    return _status;
}

QString UserStatus::message() const
{
    return _message.trimmed();
}

QUrl UserStatus::icon() const
{
    switch (_status) {
    case Away:
        return Theme::instance()->statusAwayImageSource();
    case DoNotDisturb:
        return Theme::instance()->statusDoNotDisturbImageSource();
    case Invisible:
    case Offline:
        return Theme::instance()->statusInvisibleImageSource();
    default:
        return Theme::instance()->statusOnlineImageSource();
    }
}

} // namespace OCC
