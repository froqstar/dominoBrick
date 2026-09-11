package com.dominobrick.app

import android.app.Application
import com.dominobrick.app.ble.DominoBleClient
import com.dominobrick.app.data.ChatRepository
import com.dominobrick.app.data.DeviceStore
import com.dominobrick.app.data.FrequencyStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob

class DominoApp : Application() {

    val appScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    lateinit var store: DeviceStore
        private set
    lateinit var ble: DominoBleClient
        private set
    lateinit var repo: ChatRepository
        private set
    lateinit var freqs: FrequencyStore
        private set

    override fun onCreate() {
        super.onCreate()
        store = DeviceStore(this)
        ble = DominoBleClient(appScope)
        repo = ChatRepository(appScope)
        repo.attach(ble)
        freqs = FrequencyStore(this)
        com.dominobrick.app.settings.CompanionPairing.observePresence(this)
    }
}
