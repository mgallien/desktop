#include <QLoggingCategory>
#include <QSignalSpy>
#include <QTest>

#include "pushnotificationstestutils.h"
#include "pushnotifications.h"

Q_LOGGING_CATEGORY(lcFakeWebSocketServer, "nextcloud.test.fakewebserver", QtInfoMsg)

FakeWebSocketServer::FakeWebSocketServer(quint16 port, QObject *parent)
    : QObject(parent)
    , _webSocketServer(new QWebSocketServer(QStringLiteral("Fake Server"), QWebSocketServer::NonSecureMode, this))
{
    if (!_webSocketServer->listen(QHostAddress::Any, port)) {
        Q_UNREACHABLE();
    }
    connect(_webSocketServer, &QWebSocketServer::newConnection, this, &FakeWebSocketServer::onNewConnection);
    connect(_webSocketServer, &QWebSocketServer::closed, this, &FakeWebSocketServer::closed);
    qCInfo(lcFakeWebSocketServer) << "Open fake websocket server on port:" << port;
    _processTextMessageSpy = std::make_unique<QSignalSpy>(this, &FakeWebSocketServer::processTextMessage);
    QVERIFY(_processTextMessageSpy->isValid());
}

FakeWebSocketServer::~FakeWebSocketServer()
{
    close();
}

QWebSocket *FakeWebSocketServer::authenticateAccount(const OCC::AccountPtr account)
{
    const auto pushNotifications = account->pushNotifications();
    Q_ASSERT(pushNotifications);
    QSignalSpy readySpy(pushNotifications, &OCC::PushNotifications::ready);
    Q_ASSERT(readySpy.isValid());

    // Wait for authentication
    waitForTextMessages();

    // Right authentication data should be sent
    Q_ASSERT(getTextMessagesCount() == 2);

    const auto socket = getSocketForTextMessage(0);
    const auto userSent = getTextMessage(0);
    const auto passwordSent = getTextMessage(1);

    Q_ASSERT(userSent == account->credentials()->user());
    Q_ASSERT(passwordSent == account->credentials()->password());

    // Sent authenticated
    socket->sendTextMessage("authenticated");

    // Wait for ready signal
    readySpy.wait();
    Q_ASSERT(readySpy.count() == 1);
    Q_ASSERT(account->pushNotifications()->isReady() == true);

    return socket;
}


void FakeWebSocketServer::close()
{
    if (_webSocketServer->isListening()) {
        qCInfo(lcFakeWebSocketServer) << "Close fake websocket server";

        _webSocketServer->close();
        qDeleteAll(_clients.begin(), _clients.end());
    }
}

void FakeWebSocketServer::processTextMessageInternal(const QString &message)
{
    auto client = qobject_cast<QWebSocket *>(sender());
    emit processTextMessage(client, message);
}

void FakeWebSocketServer::onNewConnection()
{
    qCInfo(lcFakeWebSocketServer) << "New connection on fake websocket server";

    auto socket = _webSocketServer->nextPendingConnection();

    connect(socket, &QWebSocket::textMessageReceived, this, &FakeWebSocketServer::processTextMessageInternal);
    connect(socket, &QWebSocket::disconnected, this, &FakeWebSocketServer::socketDisconnected);

    _clients << socket;
}

void FakeWebSocketServer::socketDisconnected()
{
    qCInfo(lcFakeWebSocketServer) << "Socket disconnected";

    auto client = qobject_cast<QWebSocket *>(sender());

    if (client) {
        _clients.removeAll(client);
        client->deleteLater();
    }
}

void FakeWebSocketServer::waitForTextMessages() const
{
    QVERIFY(_processTextMessageSpy->wait());
}

uint32_t FakeWebSocketServer::getTextMessagesCount() const
{
    return _processTextMessageSpy->count();
}

QString FakeWebSocketServer::getTextMessage(uint32_t messageNumber) const
{
    Q_ASSERT(messageNumber < _processTextMessageSpy->count());
    return _processTextMessageSpy->at(messageNumber).at(1).toString();
}

QWebSocket *FakeWebSocketServer::getSocketForTextMessage(uint32_t messageNumber) const
{
    Q_ASSERT(messageNumber < _processTextMessageSpy->count());
    return _processTextMessageSpy->at(messageNumber).at(0).value<QWebSocket *>();
}

void FakeWebSocketServer::clearTextMessages()
{
    _processTextMessageSpy->clear();
}

OCC::AccountPtr FakeWebSocketServer::createAccount(const QString &username, const QString &password)
{
    auto account = OCC::Account::create();

    QStringList typeList;
    typeList.append("files");
    typeList.append("activities");
    typeList.append("notifications");

    QString websocketUrl("ws://localhost:12345");

    QVariantMap endpointsMap;
    endpointsMap["websocket"] = websocketUrl;

    QVariantMap notifyPushMap;
    notifyPushMap["type"] = typeList;
    notifyPushMap["endpoints"] = endpointsMap;

    QVariantMap capabilitiesMap;
    capabilitiesMap["notify_push"] = notifyPushMap;

    account->setCapabilities(capabilitiesMap);

    auto credentials = new CredentialsStub(username, password);
    account->setCredentials(credentials);

    return account;
}

CredentialsStub::CredentialsStub(const QString &user, const QString &password)
    : _user(user)
    , _password(password)
{
}

QString CredentialsStub::authType() const
{
    return "";
}

QString CredentialsStub::user() const
{
    return _user;
}

QString CredentialsStub::password() const
{
    return _password;
}

QNetworkAccessManager *CredentialsStub::createQNAM() const
{
    return nullptr;
}

bool CredentialsStub::ready() const
{
    return false;
}

void CredentialsStub::fetchFromKeychain() { }

void CredentialsStub::askFromUser() { }

bool CredentialsStub::stillValid(QNetworkReply * /*reply*/)
{
    return false;
}

void CredentialsStub::persist() { }

void CredentialsStub::invalidateToken() { }

void CredentialsStub::forgetSensitiveData() { }
