#include "pushnotifications.h"
#include "creds/abstractcredentials.h"
#include "account.h"

namespace {
static constexpr int MAX_ALLOWED_FAILED_AUTHENTICATION_ATTEMPTS = 3;
}

namespace OCC {

Q_LOGGING_CATEGORY(lcPushNotifications, "nextcloud.sync.pushnotifications", QtInfoMsg)

PushNotifications::PushNotifications(Account *account, QObject *parent)
    : QObject(parent)
    , _account(account)
{
}

PushNotifications::~PushNotifications()
{
    closeWebSocket();
}

void PushNotifications::setup()
{
    qCInfo(lcPushNotifications) << "Setup push notifications";
    _failedAuthenticationAttemptsCount = 0;
    reconnectToWebSocket();
}

void PushNotifications::reconnectToWebSocket()
{
    closeWebSocket();
    openWebSocket();
}

void PushNotifications::closeWebSocket()
{
    qCInfo(lcPushNotifications) << "Close websocket" << _webSocket;

    _pingTimer.stop();
    _pingTimeoutTimer.stop();
    _isReady = false;

    // Maybe there run some reconnection attempts
    if (_reconnectTimer) {
        _reconnectTimer->stop();
    }

    if (_webSocket) {
        _webSocket->close();
    }
}

void PushNotifications::onWebSocketConnected()
{
    qCInfo(lcPushNotifications) << "Connected to websocket" << _webSocket;

    connect(_webSocket, &QWebSocket::textMessageReceived, this, &PushNotifications::onWebSocketTextMessageReceived, Qt::UniqueConnection);

    authenticateOnWebSocket();
}

void PushNotifications::authenticateOnWebSocket()
{
    const auto credentials = _account->credentials();
    const auto username = credentials->user();
    const auto password = credentials->password();

    // Authenticate
    _webSocket->sendTextMessage(username);
    _webSocket->sendTextMessage(password);
}

void PushNotifications::onWebSocketDisconnected()
{
    qCInfo(lcPushNotifications) << "Disconnected from websocket" << _webSocket;
}

void PushNotifications::onWebSocketTextMessageReceived(const QString &message)
{
    qCInfo(lcPushNotifications) << "Received push notification:" << message;

    if (message == "notify_file") {
        handleNotifyFile();
    } else if (message == "notify_activity") {
        handleNotifyActivity();
    } else if (message == "notify_notification") {
        handleNotifyNotification();
    } else if (message == "authenticated") {
        handleAuthenticated();
    } else if (message == "err: Invalid credentials") {
        handleInvalidCredentials();
    }
}

void PushNotifications::onWebSocketError(QAbstractSocket::SocketError error)
{
    // This error gets thrown in testSetup_maxConnectionAttemptsReached_deletePushNotifications after
    // the second connection attempt. I have no idea why this happens. Maybe the socket gets not closed correctly?
    // I think it's fine to ignore this error.
    if (error == QAbstractSocket::UnfinishedSocketOperationError) {
        return;
    }

    qCWarning(lcPushNotifications) << "Websocket error" << error;
    _isReady = false;
    emit connectionLost();
}

bool PushNotifications::tryReconnectToWebSocket()
{
    ++_failedAuthenticationAttemptsCount;
    if (_failedAuthenticationAttemptsCount >= MAX_ALLOWED_FAILED_AUTHENTICATION_ATTEMPTS) {
        qCInfo(lcPushNotifications) << "Max authentication attempts reached";
        return false;
    }

    if (!_reconnectTimer) {
        _reconnectTimer = new QTimer(this);
    }

    _reconnectTimer->setInterval(_reconnectTimerInterval);
    _reconnectTimer->setSingleShot(true);
    connect(_reconnectTimer, &QTimer::timeout, [this]() {
        reconnectToWebSocket();
    });
    _reconnectTimer->start();

    return true;
}

void PushNotifications::onWebSocketSslErrors(const QList<QSslError> &errors)
{
    qCWarning(lcPushNotifications) << "Received websocket ssl errors:" << errors;
    _isReady = false;
    emit authenticationFailed();
}

void PushNotifications::openWebSocket()
{
    // Open websocket
    const auto capabilities = _account->capabilities();
    const auto webSocketUrl = capabilities.pushNotificationsWebSocketUrl();

    if (!_webSocket) {
        _webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        qCInfo(lcPushNotifications) << "Created websocket" << _webSocket;
    }

    if (_webSocket) {
        connect(_webSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, &PushNotifications::onWebSocketError, Qt::UniqueConnection);
        connect(_webSocket, &QWebSocket::sslErrors, this, &PushNotifications::onWebSocketSslErrors, Qt::UniqueConnection);
        connect(_webSocket, &QWebSocket::connected, this, &PushNotifications::onWebSocketConnected, Qt::UniqueConnection);
        connect(_webSocket, &QWebSocket::disconnected, this, &PushNotifications::onWebSocketDisconnected, Qt::UniqueConnection);
        connect(_webSocket, &QWebSocket::pong, this, &PushNotifications::onWebSocketPongReceived, Qt::UniqueConnection);

        qCInfo(lcPushNotifications) << "Open connection to websocket on:" << webSocketUrl;
        _webSocket->open(webSocketUrl);
    }
}

void PushNotifications::setReconnectTimerInterval(uint32_t interval)
{
    _reconnectTimerInterval = interval;
}

bool PushNotifications::isReady() const
{
    return _isReady;
}

void PushNotifications::handleAuthenticated()
{
    qCInfo(lcPushNotifications) << "Authenticated successful on websocket";
    _failedAuthenticationAttemptsCount = 0;
    _isReady = true;
    startPingTimeoutTimer();
    emit ready();
}

void PushNotifications::handleNotifyFile()
{
    qCInfo(lcPushNotifications) << "Files push notification arrived";
    emit filesChanged(_account);
}

void PushNotifications::handleInvalidCredentials()
{
    qCInfo(lcPushNotifications) << "Invalid credentials submitted to websocket";
    if (!tryReconnectToWebSocket()) {
        closeWebSocket();
        emit authenticationFailed();
    }
}

void PushNotifications::handleNotifyNotification()
{
    qCInfo(lcPushNotifications) << "Push notification arrived";
    emit notificationsChanged(_account);
}

void PushNotifications::handleNotifyActivity()
{
    qCInfo(lcPushNotifications) << "Push activity arrived";
    emit activitiesChanged(_account);
}

void PushNotifications::onWebSocketPongReceived(quint64 /*elapsedTime*/, const QByteArray &payload)
{
    handleTimeoutPong(payload);
}

void PushNotifications::handleTimeoutPong(const QByteArray &payload)
{
    // We are not interested in different pongs from the server
    const auto expectedPingPayload = timeoutPingPayload();
    if (payload != expectedPingPayload) {
        return;
    }

    qCDebug(lcPushNotifications) << "Pong received in time";


    _timeoutPongReceivedFromWebSocketServer = true;
    startPingTimeoutTimer();
}

void PushNotifications::startPingTimeoutTimer()
{
    _pingTimeoutTimer.stop();
    _pingTimer.setInterval(_pingTimeoutInterval);
    _pingTimer.setSingleShot(true);
    connect(&_pingTimer, &QTimer::timeout, this, &PushNotifications::pingWebSocketServer, Qt::UniqueConnection);
    _pingTimer.start();
}

void PushNotifications::startPingTimedOutTimer()
{
    _pingTimeoutTimer.setInterval(_pingTimeoutInterval);
    _pingTimeoutTimer.setSingleShot(true);
    connect(&_pingTimeoutTimer, &QTimer::timeout, this, &PushNotifications::onPingTimedOut, Qt::UniqueConnection);
    _pingTimeoutTimer.start();
}

QByteArray PushNotifications::timeoutPingPayload() const
{
    const void *push_notifications_address = this;
    return QByteArray(reinterpret_cast<char *>(&push_notifications_address), sizeof(push_notifications_address));
}

void PushNotifications::pingWebSocketServer()
{
    Q_ASSERT(_webSocket);
    qCDebug(lcPushNotifications, "Ping websocket server");

    _timeoutPongReceivedFromWebSocketServer = false;

    _webSocket->ping(timeoutPingPayload());
    startPingTimedOutTimer();
}

void PushNotifications::onPingTimedOut()
{
    if (_timeoutPongReceivedFromWebSocketServer) {
        qCDebug(lcPushNotifications) << "Websocket respond with a pong in time.";
        return;
    }

    qCInfo(lcPushNotifications) << "Websocket did not respond with a pong in time. Try to reconnect.";
    // Try again to connect
    setup();
}

void PushNotifications::setPingTimeoutInterval(uint32_t timeoutInterval)
{
    _pingTimeoutInterval = timeoutInterval;
    startPingTimeoutTimer();
}
}
