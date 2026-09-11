package com.dominobrick.app.ble

import android.bluetooth.BluetoothDevice
import com.juul.kable.AndroidPeripheral
import com.juul.kable.GattStatusException
import com.juul.kable.Peripheral
import com.juul.kable.State
import com.juul.kable.WriteType
import com.juul.kable.toIdentifier
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class DominoBleClient(private val externalScope: CoroutineScope) {

    sealed interface Conn {
        data object Disconnected : Conn
        data object Connecting : Conn
        data class Connected(val mtu: Int?) : Conn
        data class Failed(val reason: String?) : Conn
    }

    private val _conn = MutableStateFlow<Conn>(Conn.Disconnected)
    val conn: StateFlow<Conn> = _conn.asStateFlow()

    private val _rxPrimary = MutableSharedFlow<Char>(extraBufferCapacity = 1024)
    val rxPrimary: SharedFlow<Char> = _rxPrimary.asSharedFlow()

    private val _rxSecondary = MutableSharedFlow<Char>(extraBufferCapacity = 1024)
    val rxSecondary: SharedFlow<Char> = _rxSecondary.asSharedFlow()

    private val _txProgress = MutableSharedFlow<Char>(extraBufferCapacity = 1024)
    val txProgress: SharedFlow<Char> = _txProgress.asSharedFlow()

    private val _currentAddress = MutableStateFlow<String?>(null)
    val currentAddress: StateFlow<String?> = _currentAddress.asStateFlow()

    private val _freqHz = MutableStateFlow<Long?>(null)
    val freqHz: StateFlow<Long?> = _freqHz.asStateFlow()

    fun injectFreqForDemo(freqHz: Long) {
        _freqHz.value = freqHz
    }

    @Volatile
    private var peripheral: Peripheral? = null
    private var linkJob: Job? = null

    @Volatile
    var demoMode = false
        private set
    private val demoTx = Channel<Char>(Channel.UNLIMITED)
    private var demoPump: Job? = null

    fun startDemo() {
        disconnect()
        demoMode = true
        _currentAddress.value = "demo"
        _conn.value = Conn.Connected(null)
        if (demoPump?.isActive != true) {
            demoPump = externalScope.launch {
                for (c in demoTx) {
                    delay(100)
                    _txProgress.emit(c)
                }
            }
        }
    }

    fun stopDemo() {
        demoMode = false
        demoPump?.cancel()
        demoPump = null
        _conn.value = Conn.Disconnected
        _currentAddress.value = null
    }

    fun demoRxPrimary(c: Char) {
        externalScope.launch { _rxPrimary.emit(c) }
    }

    fun demoRxSecondary(c: Char) {
        externalScope.launch { _rxSecondary.emit(c) }
    }

    fun connect(device: BluetoothDevice) {
        _currentAddress.value = device.address
        relaunch { Peripheral(device) }
    }

    fun connect(address: String) {
        _currentAddress.value = address
        relaunch { Peripheral(address.toIdentifier()) }
    }

    fun disconnect() {
        linkJob?.cancel()
        linkJob = null
        val p = peripheral
        peripheral = null
        _currentAddress.value = null
        _freqHz.value = null
        _conn.value = Conn.Disconnected
        if (p != null) externalScope.launch { runCatching { p.disconnect() } }
    }

    suspend fun sendBytes(data: ByteArray) {
        if (demoMode) {
            data.toString(Charsets.UTF_8).forEach { demoTx.trySend(it) }
            return
        }
        val p = peripheral ?: throw IllegalStateException("not connected")
        var offset = 0
        while (offset < data.size) {
            val end = minOf(offset + 200, data.size)
            writeWithRetry(p, data.copyOfRange(offset, end))
            offset = end
        }
    }

    suspend fun setFrequency(freqHz: Long) {
        if (demoMode) {
            _freqHz.value = freqHz
            return
        }
        val p = peripheral ?: throw IllegalStateException("not connected")
        val bytes = ByteArray(4) { i -> ((freqHz ushr (8 * i)) and 0xFF).toByte() }
        p.write(DominoUuids.FREQ, bytes, WriteType.WithResponse)
    }

    fun parseFreq(bytes: ByteArray): Long? {        if (bytes.size < 4) return null
        var value = 0L
        for (i in 0 until 4) value = value or ((bytes[i].toLong() and 0xFF) shl (8 * i))
        return value
    }

    suspend fun setSecondary(text: String) {
        if (demoMode) return
        val p = peripheral ?: throw IllegalStateException("not connected")
        val bytes = text.toByteArray(Charsets.UTF_8).take(127).toByteArray()
        writeWithRetry(p, bytes)
    }

    private suspend fun writeWithRetry(p: Peripheral, chunk: ByteArray) {
        var attempt = 0
        while (true) {
            try {
                p.write(DominoUuids.TX_DATA, chunk, WriteType.WithResponse)
                return
            } catch (e: GattStatusException) {
                if (e.status == 0x80 && attempt++ < 8) {
                    delay(250)
                    continue
                }
                throw e
            }
        }
    }

    private fun relaunch(make: () -> Peripheral) {
        linkJob?.cancel()
        peripheral = null
        linkJob = externalScope.launch { runLink(make()) }
    }

    private suspend fun CoroutineScope.runLink(p: Peripheral) {
        peripheral = p
        _conn.value = Conn.Connecting
        try {
            p.connect()
            val mtu = (p as? AndroidPeripheral)?.let { runCatching { it.requestMtu(240) }.getOrNull() }
            _conn.value = Conn.Connected(mtu)
            launchCollector(p, DominoUuids.RX_PRIMARY, _rxPrimary)
            launchCollector(p, DominoUuids.RX_SECONDARY, _rxSecondary)
            launchCollector(p, DominoUuids.TX_PROGRESS, _txProgress)
            launch { runCatching { parseFreq(p.read(DominoUuids.FREQ))?.let { _freqHz.value = it } } }
            launch {
                runCatching {
                    p.observe(DominoUuids.FREQ).collect { parseFreq(it)?.let { hz -> _freqHz.value = hz } }
                }
            }
            p.state.collect { if (it is State.Disconnected) throw IllegalStateException("link lost") }
        } catch (e: Exception) {
            if (peripheral === p) {
                peripheral = null
                _currentAddress.value = null
                _freqHz.value = null
                _conn.value = if (e is IllegalStateException && e.message == "link lost") {
                    Conn.Disconnected
                } else {
                    Conn.Failed(e.message)
                }
            }
        }
    }

    private fun CoroutineScope.launchCollector(
        p: Peripheral,
        characteristic: com.juul.kable.Characteristic,
        sink: MutableSharedFlow<Char>,
    ) {
        launch {
            p.observe(characteristic).collect { bytes ->
                bytes.toString(Charsets.UTF_8).forEach { sink.emit(it) }
            }
        }
    }
}
