package com.dominobrick.app.data

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

class DeviceStore(context: Context) {

    private val prefs = context.getSharedPreferences("dominobrick", Context.MODE_PRIVATE)

    private val _macs = MutableStateFlow(loadMacsSync())
    val macs: StateFlow<Set<String>> = _macs.asStateFlow()

    private val _names = MutableStateFlow(loadNamesSync())
    val names: StateFlow<Map<String, String>> = _names.asStateFlow()

    fun nameFor(mac: String): String? = _names.value[mac]

    fun saveName(mac: String, name: String) {
        val updated = if (name.isBlank()) _names.value - mac else _names.value + (mac to name.trim())
        prefs.edit().putStringSet(KEY_NAMES, updated.map { "${it.key}|${it.value}" }.toSet()).apply()
        _names.value = updated
    }

    private fun loadNamesSync(): Map<String, String> =
        prefs.getStringSet(KEY_NAMES, emptySet())?.mapNotNull {
            val i = it.indexOf('|')
            if (i < 0) null else it.substring(0, i) to it.substring(i + 1)
        }?.toMap() ?: emptyMap()

    private val _secondary = MutableStateFlow(prefs.getString(KEY_SECONDARY, "") ?: "")
    val secondary: StateFlow<String> = _secondary.asStateFlow()

    fun saveSecondary(text: String) {
        prefs.edit().putString(KEY_SECONDARY, text).apply()
        _secondary.value = text
    }

    private val _mycall = MutableStateFlow(prefs.getString(KEY_MYCALL, "") ?: "")
    val mycall: StateFlow<String> = _mycall.asStateFlow()

    fun saveMyCall(text: String) {
        val v = text.trim().uppercase()
        prefs.edit().putString(KEY_MYCALL, v).apply()
        _mycall.value = v
    }

    fun loadMac(): String? = _macs.value.firstOrNull()

    fun saveMac(mac: String) {
        val updated = _macs.value + mac
        prefs.edit().putStringSet(KEY_MACS, updated).apply()
        _macs.value = updated
    }

    fun removeMac(mac: String) {
        val updated = _macs.value - mac
        prefs.edit().putStringSet(KEY_MACS, updated).apply()
        _macs.value = updated
        saveName(mac, "")
    }

    fun clearMac() {
        prefs.edit().remove(KEY_MACS).apply()
        _macs.value = emptySet()
    }

    private fun loadMacsSync(): Set<String> =
        prefs.getStringSet(KEY_MACS, null)?.toSet() ?: prefs.getString(KEY_MAC, null)?.let {
            setOf(it)
        } ?: emptySet()

    companion object {
        private const val KEY_MACS = "bonded_macs"
        private const val KEY_MAC = "bonded_mac"
        private const val KEY_SECONDARY = "secondary_text"
        private const val KEY_MYCALL = "mycall"
        private const val KEY_NAMES = "device_names"
    }
}
