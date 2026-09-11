package com.dominobrick.app.demo

import com.dominobrick.app.ble.DominoBleClient
import com.dominobrick.app.data.Channel
import com.dominobrick.app.data.ChatRepository
import kotlinx.coroutines.delay
import kotlin.random.Random

object DemoFeed {

    private data class Station(val secondary: String, val primary: String, val freqHz: Long)

    private val script = listOf(
        Station(
            "CQ CQ DO3DEL is on air ",
            "CQ CQ DE DO3DEL DO3DEL K ",
            14_074_000,
        ),
        Station(
            "DL1ABC portable on 40m ",
            "DE DL1ABC UR 599 QTH MUNICH OP KARL BK ",
            7_074_000,
        ),
        Station(
            "K7QO calling CQ ",
            "K7QO DE JA1XYZ TU ES 73 SK ",
            14_080_000,
        ),
        Station(
            "JA1XYZ Tokyo ",
            "TU FER CALL ES RPRT 579 QSB BK ",
            21_074_000,
        ),
    )

    suspend fun run(repo: ChatRepository, ble: DominoBleClient? = null) {
        ble?.startDemo()
        try {
            var idx = 0
            while (true) {
                val station = script[idx % script.size]
                idx++
                ble?.injectFreqForDemo(station.freqHz)
                var secPos = 0
                station.primary.forEach { c ->
                    if (Random.nextFloat() < 0.35f) {
                        repeat(Random.nextInt(1, 4)) {
                            emitSecondary(ble, repo, station.secondary[secPos % station.secondary.length])
                            secPos++
                            delay(30)
                        }
                    }
                    emitPrimary(ble, repo, c)
                    delay(125)
                }
                delay(3000)
            }
        } finally {
            ble?.stopDemo()
        }
    }

    private fun emitPrimary(ble: DominoBleClient?, repo: ChatRepository, c: Char) {
        if (ble != null && ble.demoMode) ble.demoRxPrimary(c)
        else repo.addReceived(Channel.PRIMARY, c)
    }

    private fun emitSecondary(ble: DominoBleClient?, repo: ChatRepository, c: Char) {
        if (ble != null && ble.demoMode) ble.demoRxSecondary(c)
        else repo.addReceived(Channel.SECONDARY, c)
    }
}
