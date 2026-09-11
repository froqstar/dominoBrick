package com.dominobrick.app.data

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

class DeviceStore(context: Context) {

    private val prefs = context.getSharedPreferences("dominobrick", Context.MODE_PRIVATE)

    private val _macs = MutableStateFlow(loadMacsSync())
    val macs: StateFlow<Set<String>> = _macs.asStateFlow()

    private val _secondary = MutableStateFlow(prefs.getString(KEY_SECONDARY, "") ?: "")
    val secondary: StateFlow<String> = _secondary.asStateFlow()

    fun saveSecondary(text: String) {
        prefs.edit().putString(KEY_SECONDARY, text).apply()
        _secondary.value = text
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
    }
}
