package com.dominobrick.app.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.dominobrick.app.ble.DominoBleClient
import com.dominobrick.app.data.ChatRepository
import com.dominobrick.app.data.DeviceStore
import com.dominobrick.app.service.DominoLinkService
import com.dominobrick.app.settings.CompanionPairing
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

enum class DeviceStatus { CONNECTED, CONNECTING, RETRYING, IDLE }

data class PairedDevice(val mac: String, val status: DeviceStatus)

class SettingsViewModel(
    private val appContext: android.content.Context,
    private val ble: DominoBleClient,
    private val store: DeviceStore,
    private val freqs: com.dominobrick.app.data.FrequencyStore,
    repo: ChatRepository,
) : ViewModel() {

    val secondaryHint = repo.lastSecondaryText

    private val _pairError = MutableStateFlow<String?>(null)
    val pairError: StateFlow<String?> = _pairError.asStateFlow()

    private val _secondary = MutableStateFlow(store.secondary.value)
    val secondary: StateFlow<String> = _secondary.asStateFlow()

    val frequencies = freqs.frequencies

    fun addFrequency(name: String, freqHz: Long) {
        if (name.isBlank()) return
        freqs.add(name.trim(), freqHz)
    }

    fun removeFrequency(id: Long) {
        freqs.remove(id)
    }

    val devices: StateFlow<List<PairedDevice>> = combine(
        store.macs, ble.conn, ble.currentAddress,
    ) { macs, conn, current ->
        macs.sorted().map { mac ->
            val status = if (current != null && current.equals(mac, ignoreCase = true)) {
                when (conn) {
                    is DominoBleClient.Conn.Connected -> DeviceStatus.CONNECTED
                    is DominoBleClient.Conn.Connecting -> DeviceStatus.CONNECTING
                    is DominoBleClient.Conn.Failed -> DeviceStatus.RETRYING
                    is DominoBleClient.Conn.Disconnected -> DeviceStatus.IDLE
                }
            } else {
                DeviceStatus.IDLE
            }
            PairedDevice(mac, status)
        }
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), emptyList())

    init {
        viewModelScope.launch {
            ble.conn.map { it is DominoBleClient.Conn.Connected }
                .distinctUntilChanged()
                .filter { it }
                .collect { pushSecondary() }
        }
    }

    fun onPaired(address: String) {
        store.saveMac(address)
        _pairError.value = null
        CompanionPairing.observePresence(appContext)
        DominoLinkService.start(appContext, address)
    }

    fun onPairFailed(error: CharSequence?) {
        val msg = error?.toString()?.trim()
        if (msg.isNullOrEmpty() || msg.equals("canceled", ignoreCase = true) ||
            msg.equals("cancelled", ignoreCase = true)
        ) {
            return
        }
        _pairError.value = msg
    }

    fun connect(mac: String) {
        DominoLinkService.start(appContext, mac)
    }

    fun unpair(mac: String) {
        CompanionPairing.forget(appContext, mac)
        CompanionPairing.stopObserving(appContext)
        store.removeMac(mac)
        CompanionPairing.observePresence(appContext)
        if (ble.currentAddress.value?.equals(mac, ignoreCase = true) == true) {
            DominoLinkService.stop(appContext)
            ble.disconnect()
        }
    }

    private var secondaryJob: Job? = null

    fun onSecondaryChange(text: String) {
        _secondary.value = text
        store.saveSecondary(text)
        secondaryJob?.cancel()
        secondaryJob = viewModelScope.launch {
            delay(400)
            pushSecondary()
        }
    }

    private suspend fun pushSecondary() {
        val text = _secondary.value
        if (text.isEmpty()) return
        runCatching { ble.setSecondary(text) }
    }
}
