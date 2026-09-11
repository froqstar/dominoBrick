package com.dominobrick.app.settings

import android.bluetooth.BluetoothDevice
import android.bluetooth.le.ScanFilter
import android.companion.AssociationInfo
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.Context
import android.content.Intent
import android.content.IntentSender
import android.os.ParcelUuid
import androidx.activity.result.ActivityResult
import com.dominobrick.app.ble.DominoUuids
import com.dominobrick.app.data.DeviceStore

object CompanionPairing {

    fun buildRequest(): AssociationRequest {
        val scanFilter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(java.util.UUID.fromString(DominoUuids.SERVICE.toString())))
            .build()
        val deviceFilter = BluetoothLeDeviceFilter.Builder()
            .setScanFilter(scanFilter)
            .build()
        return AssociationRequest.Builder()
            .addDeviceFilter(deviceFilter)
            .setSingleDevice(true)
            .build()
    }

    fun associate(
        context: Context,
        onPicker: (IntentSender) -> Unit,
        onAssociated: (String) -> Unit,
        onError: (CharSequence?) -> Unit,
    ) {
        val cdm = context.getSystemService(CompanionDeviceManager::class.java) ?: return
        runCatching {
            cdm.associate(
                buildRequest(),
                object : CompanionDeviceManager.Callback() {
                    override fun onAssociationPending(intentSender: IntentSender) {
                        onPicker(intentSender)
                    }

                    override fun onAssociationCreated(associationInfo: AssociationInfo) {
                        associationInfo.deviceMacAddress?.toString()?.let { onAssociated(it) }
                    }

                    override fun onFailure(error: CharSequence?) {
                        onError(error)
                    }
                },
                null,
            )
        }.onFailure { onError(it.message) }
    }

    fun deviceFromResult(result: ActivityResult): BluetoothDevice? {
        val data: Intent = result.data ?: return null
        return data.getParcelableExtra(CompanionDeviceManager.EXTRA_DEVICE, BluetoothDevice::class.java)
    }

    fun forget(context: Context, address: String) {
        runCatching {
            context.getSystemService(CompanionDeviceManager::class.java)?.disassociate(address)
        }
    }

    fun observePresence(context: Context): Boolean {
        val cdm = context.getSystemService(CompanionDeviceManager::class.java) ?: return false
        val macs = DeviceStore(context).macs.value
        if (macs.isEmpty()) return false
        val assocs = runCatching { cdm.myAssociations }.getOrNull() ?: return false
        var armed = false
        macs.forEach { mac ->
            val assoc = assocs.firstOrNull {
                it.deviceMacAddress?.toString().equals(mac, ignoreCase = true)
            } ?: return@forEach
            val request = android.companion.ObservingDevicePresenceRequest.Builder()
                .setAssociationId(assoc.id)
                .build()
            runCatching {
                cdm.startObservingDevicePresence(request)
                armed = true
            }
        }
        return armed
    }

    fun stopObserving(context: Context) {
        val cdm = context.getSystemService(CompanionDeviceManager::class.java) ?: return
        val assocs = runCatching { cdm.myAssociations }.getOrNull() ?: return
        assocs.forEach { assoc ->
            val request = android.companion.ObservingDevicePresenceRequest.Builder()
                .setAssociationId(assoc.id)
                .build()
            runCatching { cdm.stopObservingDevicePresence(request) }
        }
    }
}
