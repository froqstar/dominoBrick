package com.dominobrick.app.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.dominobrick.app.ble.DominoBleClient
import com.dominobrick.app.data.FrequencyStore
import kotlinx.coroutines.launch

class FreqViewModel(
    private val ble: DominoBleClient,
    private val freqs: FrequencyStore,
) : ViewModel() {

    val freqHz = ble.freqHz
    val favorites = freqs.frequencies

    fun select(freqHz: Long) {
        viewModelScope.launch {
            runCatching { ble.setFrequency(freqHz) }
        }
    }

    fun saveFavorite(name: String, freqHz: Long) {
        freqs.add(name, freqHz)
    }
}
