package com.dominobrick.app.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import com.dominobrick.app.DominoApp
import com.dominobrick.app.MainActivity
import com.dominobrick.app.ble.DominoBleClient
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class DominoLinkService : Service() {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var loop: Job? = null
    private var watching = false

    private val app: DominoApp
        get() = application as DominoApp

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL, "dominoBrick link", NotificationManager.IMPORTANCE_LOW),
        )
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            wanted = false
            app.ble.disconnect()
            stopSelf()
            return START_NOT_STICKY
        }
        wanted = true
        startForeground(NOTIF_ID, buildNotification(app.ble.conn.value))
        watchConn()
        ensureLoop()
        return START_STICKY
    }

    override fun onDestroy() {
        loop?.cancel()
        scope.cancel()
        super.onDestroy()
    }

    private fun watchConn() {
        if (watching) return
        watching = true
        scope.launch {
            app.ble.conn.collect {
                getSystemService(NotificationManager::class.java)
                    .notify(NOTIF_ID, buildNotification(it))
            }
        }
    }

    private fun ensureLoop() {
        if (loop?.isActive == true) return
        loop = scope.launch {
            var backoffMs = 5_000L
            while (wanted) {
                val stored = app.store.macs.value
                val mac = targetAddress?.takeIf { stored.contains(it) }
                    ?: stored.firstOrNull()
                if (mac == null) break
                if (targetAddress != null && targetAddress != app.ble.currentAddress.value &&
                    app.ble.conn.value is DominoBleClient.Conn.Connected
                ) {
                    app.ble.disconnect()
                }
                when (app.ble.conn.value) {
                    is DominoBleClient.Conn.Connected -> {
                        backoffMs = 5_000L
                        delay(10_000)
                    }
                    is DominoBleClient.Conn.Connecting -> delay(3_000)
                    else -> {
                        app.ble.connect(mac)
                        delay(20_000)
                        if (app.ble.conn.value !is DominoBleClient.Conn.Connected) {
                            delay(backoffMs)
                            backoffMs = minOf(backoffMs * 2, 60_000)
                        }
                    }
                }
            }
            stopSelf()
        }
    }

    private fun buildNotification(conn: DominoBleClient.Conn): Notification {
        val content = Intent(this, MainActivity::class.java)
        val contentPi = PendingIntent.getActivity(
            this, 0, content,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val stopPi = PendingIntent.getService(
            this, 0, Intent(this, DominoLinkService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle("dominoBrick link")
            .setContentText(connText(conn))
            .setContentIntent(contentPi)
            .setOngoing(true)
            .addAction(
                Notification.Action.Builder(null, "Stop", stopPi).build(),
            )
            .build()
    }

    private fun connText(conn: DominoBleClient.Conn): String = when (conn) {
        is DominoBleClient.Conn.Connected -> "connected"
        is DominoBleClient.Conn.Connecting -> "connecting…"
        is DominoBleClient.Conn.Failed -> "retrying…"
        is DominoBleClient.Conn.Disconnected -> "waiting for device…"
    }

    companion object {
        const val ACTION_START = "com.dominobrick.app.LINK_START"
        const val ACTION_STOP = "com.dominobrick.app.LINK_STOP"
        private const val CHANNEL = "domino_link"
        private const val NOTIF_ID = 1

        @Volatile
        var wanted = false

        @Volatile
        var targetAddress: String? = null

        fun start(context: Context, address: String? = null) {
            wanted = true
            if (address != null) targetAddress = address
            val intent = Intent(context, DominoLinkService::class.java).setAction(ACTION_START)
            runCatching { context.startForegroundService(intent) }
        }

        fun stop(context: Context) {
            wanted = false
            targetAddress = null
            runCatching { context.stopService(Intent(context, DominoLinkService::class.java)) }
        }
    }
}
