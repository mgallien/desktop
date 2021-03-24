#include <QTest>
#include <QVector>
#include <QWebSocketServer>
#include <QSignalSpy>

#include "pushnotifications.h"
#include "pushnotificationstestutils.h"

class TestPushNotifications : public QObject
{
    Q_OBJECT

private slots:
    void testSetup_correctCredentials_authenticateAndEmitReady()
    {
        FakeWebSocketServer fakeServer;
        auto account = FakeWebSocketServer::createAccount();

        fakeServer.authenticateAccount(account);
    }

    void testOnWebSocketTextMessageReceived_notifyFileMessage_emitFilesChanged()
    {
        FakeWebSocketServer fakeServer;
        auto account = FakeWebSocketServer::createAccount();
        const auto socket = fakeServer.authenticateAccount(account);
        QSignalSpy filesChangedSpy(account->pushNotifications(), &OCC::PushNotifications::filesChanged);
        QVERIFY(filesChangedSpy.isValid());

        socket->sendTextMessage("notify_file");

        // filesChanged signal should be emitted
        QVERIFY(filesChangedSpy.wait());
        QCOMPARE(filesChangedSpy.count(), 1);
        auto accountFilesChanged = filesChangedSpy.at(0).at(0).value<OCC::Account *>();
        QCOMPARE(accountFilesChanged, account.data());
    }

    void testOnWebSocketTextMessageReceived_notifyActivityMessage_emitNotification()
    {
        FakeWebSocketServer fakeServer;
        auto account = FakeWebSocketServer::createAccount();
        const auto socket = fakeServer.authenticateAccount(account);
        QSignalSpy activitySpy(account->pushNotifications(), &OCC::PushNotifications::activitiesChanged);
        QVERIFY(activitySpy.isValid());

        // Send notify_file push notification
        socket->sendTextMessage("notify_activity");

        // notification signal should be emitted
        QVERIFY(activitySpy.wait());
        QCOMPARE(activitySpy.count(), 1);
        auto accountFilesChanged = activitySpy.at(0).at(0).value<OCC::Account *>();
        QCOMPARE(accountFilesChanged, account.data());
    }

    void testOnWebSocketTextMessageReceived_notifyNotificationMessage_emitNotification()
    {
        FakeWebSocketServer fakeServer;
        auto account = FakeWebSocketServer::createAccount();
        const auto socket = fakeServer.authenticateAccount(account);
        QSignalSpy notificationSpy(account->pushNotifications(), &OCC::PushNotifications::notificationsChanged);
        QVERIFY(notificationSpy.isValid());

        // Send notify_file push notification
        socket->sendTextMessage("notify_notification");

        // notification signal should be emitted
        QVERIFY(notificationSpy.wait());
        QCOMPARE(notificationSpy.count(), 1);
        auto accountFilesChanged = notificationSpy.at(0).at(0).value<OCC::Account *>();
        QCOMPARE(accountFilesChanged, account.data());
    }

    void testOnWebSocketTextMessageReceived_invalidCredentialsMessage_reconnectWebSocket()
    {
        FakeWebSocketServer fakeServer;
        auto account = FakeWebSocketServer::createAccount();
        // Need to set reconnect timer interval to zero for tests
        account->pushNotifications()->setReconnectTimerInterval(0);

        // Wait for authentication attempt and then sent invalid credentials
        fakeServer.waitForTextMessages();
        QCOMPARE(fakeServer.getTextMessagesCount(), 2);
        const auto socket = fakeServer.getSocketForTextMessage(0);
        const auto firstPasswordSent = fakeServer.getTextMessage(1);
        QCOMPARE(firstPasswordSent, account->credentials()->password());
        fakeServer.clearTextMessages();
        socket->sendTextMessage("err: Invalid credentials");

        // Wait for a new authentication attempt
        fakeServer.waitForTextMessages();
        QCOMPARE(fakeServer.getTextMessagesCount(), 2);
        const auto secondPasswordSent = fakeServer.getTextMessage(1);
        QCOMPARE(secondPasswordSent, account->credentials()->password());
    }

    void testOnWebSocketError_connectionLost_emitConnectionLost()
    {
        FakeWebSocketServer fakeServer;
        auto account = FakeWebSocketServer::createAccount();
        QSignalSpy connectionLostSpy(account->pushNotifications(), &OCC::PushNotifications::connectionLost);
        QVERIFY(connectionLostSpy.isValid());

        // Wait for authentication and then sent a network error
        fakeServer.waitForTextMessages();
        QCOMPARE(fakeServer.getTextMessagesCount(), 2);
        auto socket = fakeServer.getSocketForTextMessage(0);
        socket->abort();

        QVERIFY(connectionLostSpy.wait());
        // Account handled connectionLost signal and deleted PushNotifications
        QCOMPARE(account->pushNotifications(), nullptr);
    }

    void testSetup_maxConnectionAttemptsReached_deletePushNotifications()
    {
        FakeWebSocketServer fakeServer;
        auto account = FakeWebSocketServer::createAccount();
        account->pushNotifications()->setReconnectTimerInterval(0);
        QSignalSpy authenticationFailedSpy(account->pushNotifications(), &OCC::PushNotifications::authenticationFailed);
        QVERIFY(authenticationFailedSpy.isValid());

        // Let three authentication attempts fail
        for (uint8_t i = 0; i < 3; ++i) {
            fakeServer.waitForTextMessages();
            QCOMPARE(fakeServer.getTextMessagesCount(), 2);
            auto socket = fakeServer.getSocketForTextMessage(0);
            fakeServer.clearTextMessages();
            socket->sendTextMessage("err: Invalid credentials");
        }

        // Now the authenticationFailed Signal should be emitted
        QVERIFY(authenticationFailedSpy.wait());
        QCOMPARE(authenticationFailedSpy.count(), 1);
        // Account deleted the push notifications
        QCOMPARE(account->pushNotifications(), nullptr);
    }

    void testOnWebSocketSslError_sslError_deletePushNotifications()
    {
        FakeWebSocketServer fakeServer;
        auto account = FakeWebSocketServer::createAccount();

        fakeServer.waitForTextMessages();
        // FIXME: This a little bit ugly but I had no better idea how to trigger a error on the websocket client.
        // The websocket that is retrived through the server is not connected to the ssl error signal.
        auto pushNotificationsWebSocketChildren = account->pushNotifications()->findChildren<QWebSocket *>();
        QVERIFY(pushNotificationsWebSocketChildren.size() == 1);
        emit pushNotificationsWebSocketChildren[0]->sslErrors(QList<QSslError>());

        // Account handled connectionLost signal and deleted PushNotifications
        QCOMPARE(account->pushNotifications(), nullptr);
    }

    void testAccount_web_socket_connectionLost_emitNotificationsDisabled()
    {
        FakeWebSocketServer fakeServer;
        auto account = FakeWebSocketServer::createAccount();
        // Need to set reconnect timer interval to zero for tests
        account->pushNotifications()->setReconnectTimerInterval(0);
        const auto socket = fakeServer.authenticateAccount(account);

        QSignalSpy connectionLostSpy(account->pushNotifications(), &OCC::PushNotifications::connectionLost);
        QVERIFY(connectionLostSpy.isValid());

        QSignalSpy pushNotificationsDisabledSpy(account.data(), &OCC::Account::pushNotificationsDisabled);
        QVERIFY(pushNotificationsDisabledSpy.isValid());

        // Wait for authentication and then sent a network error
        socket->abort();

        QVERIFY(pushNotificationsDisabledSpy.wait());
        QCOMPARE(pushNotificationsDisabledSpy.count(), 1);

        QCOMPARE(connectionLostSpy.count(), 1);

        auto accountSent = pushNotificationsDisabledSpy.at(0).at(0).value<OCC::Account *>();
        QCOMPARE(accountSent, account.data());
    }

    void testAccount_web_socket_authenticationFailed_emitNotificationsDisabled()
    {
        FakeWebSocketServer fakeServer;
        auto account = FakeWebSocketServer::createAccount();
        account->pushNotifications()->setReconnectTimerInterval(0);
        QSignalSpy authenticationFailedSpy(account->pushNotifications(), &OCC::PushNotifications::authenticationFailed);
        QVERIFY(authenticationFailedSpy.isValid());
        QSignalSpy pushNotificationsDisabledSpy(account.data(), &OCC::Account::pushNotificationsDisabled);
        QVERIFY(pushNotificationsDisabledSpy.isValid());

        // Let three authentication attempts fail
        for (uint8_t i = 0; i < 3; ++i) {
            fakeServer.waitForTextMessages();
            QCOMPARE(fakeServer.getTextMessagesCount(), 2);
            auto socket = fakeServer.getSocketForTextMessage(0);
            fakeServer.clearTextMessages();
            socket->sendTextMessage("err: Invalid credentials");
        }

        // Now the authenticationFailed and pushNotificationsDisabled Signals should be emitted
        QVERIFY(pushNotificationsDisabledSpy.wait());
        QCOMPARE(pushNotificationsDisabledSpy.count(), 1);
        QCOMPARE(authenticationFailedSpy.count(), 1);
        auto accountSent = pushNotificationsDisabledSpy.at(0).at(0).value<OCC::Account *>();
        QCOMPARE(accountSent, account.data());
    }

    void testPingTimeout_pingTimedOut_reconnect()
    {
        FakeWebSocketServer fakeServer;
        auto account = FakeWebSocketServer::createAccount();
        fakeServer.authenticateAccount(account);

        // Set the ping timeout interval to zero and check if the server attemps to authenticate again
        fakeServer.clearTextMessages();
        account->pushNotifications()->setPingTimeoutInterval(0);
        fakeServer.authenticateAccount(account);
    }
};

QTEST_GUILESS_MAIN(TestPushNotifications)
#include "testpushnotifications.moc"
