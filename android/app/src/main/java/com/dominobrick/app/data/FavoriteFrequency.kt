package com.dominobrick.app.data

data class FavoriteFrequency(val id: Long, val name: String, val freqHz: Long) {
    fun display(): String = "${mhzInput(freqHz)} MHz"

    fun serialize(): String = "$id|$name|$freqHz"

    companion object {
        fun mhzInput(freqHz: Long): String =
            "%.6f".format(freqHz / 1_000_000.0).trimEnd('0').trimEnd('.')
        fun parse(raw: String): FavoriteFrequency? {
            val parts = raw.split('|')
            if (parts.size != 3) return null
            val id = parts[0].toLongOrNull() ?: return null
            val freq = parts[2].toLongOrNull() ?: return null
            return FavoriteFrequency(id, parts[1], freq)
        }

        fun parseMhz(input: String): Long? {
            val mhz = input.trim().replace(',', '.').toDoubleOrNull() ?: return null
            if (mhz <= 0 || mhz > 1000) return null
            return (mhz * 1_000_000).toLong()
        }
    }
}
