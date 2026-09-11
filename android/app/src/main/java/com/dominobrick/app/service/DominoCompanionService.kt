package com.dominobrick.app.service

import android.companion.AssociationInfo
import android.companion.CompanionDeviceService
import com.dominobrick.app.data.DeviceStore

class DominoCompanionService : CompanionDeviceService() {

    override fun onDeviceAppeared(associationInfo: AssociationInfo) {
        val seen = associationInfo.deviceMacAddress?.toString() ?: return
        val saved = DeviceStore(this).macs.value
        if (saved.isNotEmpty() && saved.none { it.equals(seen, ignoreCase = true) }) return
        DominoLinkService.start(this, seen)
    }

    override fun onDeviceDisappeared(associationInfo: AssociationInfo) {
    }
}
