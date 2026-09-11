package com.dominobrick.app.ble

import com.juul.kable.Characteristic
import com.juul.kable.characteristicOf
import kotlin.uuid.Uuid

object DominoUuids {
    val SERVICE: Uuid = Uuid.parse("d0b10000-bbaa-9988-7766-554433221100")
    val TX_DATA: Characteristic = characteristicOf(
        Uuid.parse("d0b10000-bbaa-9988-7766-554433221100"),
        Uuid.parse("d0b10001-bbaa-9988-7766-554433221100"),
    )
    val TX_PROGRESS: Characteristic = characteristicOf(
        Uuid.parse("d0b10000-bbaa-9988-7766-554433221100"),
        Uuid.parse("d0b10002-bbaa-9988-7766-554433221100"),
    )
    val SEC_MSG: Characteristic = characteristicOf(
        Uuid.parse("d0b10000-bbaa-9988-7766-554433221100"),
        Uuid.parse("d0b10003-bbaa-9988-7766-554433221100"),
    )
    val RX_PRIMARY: Characteristic = characteristicOf(
        Uuid.parse("d0b10000-bbaa-9988-7766-554433221100"),
        Uuid.parse("d0b10004-bbaa-9988-7766-554433221100"),
    )
    val RX_SECONDARY: Characteristic = characteristicOf(
        Uuid.parse("d0b10000-bbaa-9988-7766-554433221100"),
        Uuid.parse("d0b10005-bbaa-9988-7766-554433221100"),
    )
    val FREQ: Characteristic = characteristicOf(
        Uuid.parse("d0b10000-bbaa-9988-7766-554433221100"),
        Uuid.parse("d0b10006-bbaa-9988-7766-554433221100"),
    )
}
