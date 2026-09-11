package com.dominobrick.app.data

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

class FrequencyStore(context: Context) {

    private val prefs = context.getSharedPreferences("dominobrick", Context.MODE_PRIVATE)

    private val _frequencies = MutableStateFlow(loadSync())
    val frequencies: StateFlow<List<FavoriteFrequency>> = _frequencies.asStateFlow()

    fun add(name: String, freqHz: Long) {
        val updated = _frequencies.value + FavoriteFrequency(System.currentTimeMillis(), name, freqHz)
        save(updated)
    }

    fun remove(id: Long) {
        save(_frequencies.value.filter { it.id != id })
    }

    private fun save(list: List<FavoriteFrequency>) {
        prefs.edit().putStringSet(KEY_FREQS, list.map { it.serialize() }.toSet()).apply()
        _frequencies.value = list
    }

    private fun loadSync(): List<FavoriteFrequency> =
        prefs.getStringSet(KEY_FREQS, emptySet())
            ?.mapNotNull { FavoriteFrequency.parse(it) }
            ?.sortedBy { it.freqHz } ?: emptyList()

    companion object {
        private const val KEY_FREQS = "favorite_freqs"
    }
}
