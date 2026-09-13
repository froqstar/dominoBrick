package com.dominobrick.app.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.dominobrick.app.settings.CompanionPairing

@Composable
fun SettingsScreen(vm: SettingsViewModel) {
    val context = LocalContext.current
    val devices by vm.devices.collectAsState()
    val pairError by vm.pairError.collectAsState()
    val secondary by vm.secondary.collectAsState()
    val frequencies by vm.frequencies.collectAsState()
    var showFreqDialog by remember { mutableStateOf(false) }
    var editingFreq by remember { mutableStateOf<com.dominobrick.app.data.FavoriteFrequency?>(null) }
    var renamingMac by remember { mutableStateOf<String?>(null) }

    val picker = rememberLauncherForActivityResult(
        ActivityResultContracts.StartIntentSenderForResult(),
    ) { result ->
        if (result.resultCode != android.app.Activity.RESULT_OK) return@rememberLauncherForActivityResult
        CompanionPairing.deviceFromResult(result)?.let { vm.onPaired(it.address) }
    }

    Column(Modifier.fillMaxSize().imePadding().verticalScroll(rememberScrollState()).padding(16.dp)) {
        pairError?.let {
            Text(it, color = MaterialTheme.colorScheme.error)
        }
        if (devices.isEmpty()) {
            Text(
                "tap + to pair dominoBrick",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(vertical = 8.dp),
            )
        }
        devices.forEach { device ->
            val connected = device.status == DeviceStatus.CONNECTED
            ListItem(
                headlineContent = { Text(device.name ?: device.mac) },
                supportingContent = {
                    Text(
                        if (device.name != null) "${device.mac} · ${statusText(device.status)}"
                        else statusText(device.status),
                    )
                },
                leadingContent = {
                    val dot = when (device.status) {
                        DeviceStatus.CONNECTED -> MaterialTheme.colorScheme.primary
                        DeviceStatus.CONNECTING, DeviceStatus.RETRYING ->
                            MaterialTheme.colorScheme.tertiary
                        DeviceStatus.IDLE -> MaterialTheme.colorScheme.outline
                    }
                    Canvas(Modifier.size(12.dp)) { drawCircle(dot) }
                },
                trailingContent = {
                    IconButton(onClick = { vm.unpair(device.mac) }) {
                        Icon(Icons.Filled.Close, contentDescription = "Unpair")
                    }
                },
                colors = ListItemDefaults.colors(
                    containerColor = if (connected) {
                        MaterialTheme.colorScheme.primaryContainer
                    } else {
                        Color.Transparent
                    },
                ),
                modifier = Modifier.clickable { renamingMac = device.mac },
            )
        }
        Row(
            Modifier.fillMaxWidth().padding(vertical = 12.dp),
            horizontalArrangement = Arrangement.Center,
        ) {
            FilledTonalIconButton(
                onClick = {
                    CompanionPairing.associate(
                        context,
                        onPicker = { sender ->
                            picker.launch(
                                androidx.activity.result.IntentSenderRequest.Builder(sender)
                                    .build(),
                            )
                        },
                        onAssociated = { vm.onPaired(it) },
                        onError = { vm.onPairFailed(it) },
                    )
                },
                modifier = Modifier.size(56.dp),
            ) {
                Icon(Icons.Filled.Add, contentDescription = "Pair new device")
            }
        }
        HorizontalDivider(Modifier.padding(vertical = 8.dp))
        Text(
            "Favorite frequencies",
            style = MaterialTheme.typography.titleSmall,
            modifier = Modifier.padding(bottom = 4.dp),
        )
        if (frequencies.isEmpty()) {
            Text(
                "tap + to add favorite frequency",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(vertical = 8.dp),
            )
        }
        frequencies.forEach { freq ->
            ListItem(
                headlineContent = { Text(freq.name) },
                supportingContent = { Text(freq.display()) },
                trailingContent = {
                    IconButton(onClick = { vm.removeFrequency(freq.id) }) {
                        Icon(Icons.Filled.Close, contentDescription = "Remove")
                    }
                },
                modifier = Modifier.clickable { editingFreq = freq },
            )
        }
        Row(
            Modifier.fillMaxWidth().padding(vertical = 12.dp),
            horizontalArrangement = Arrangement.Center,
        ) {
            FilledTonalIconButton(
                onClick = { showFreqDialog = true },
                modifier = Modifier.size(56.dp),
            ) {
                Icon(Icons.Filled.Add, contentDescription = "Add frequency")
            }
        }
        HorizontalDivider(Modifier.padding(vertical = 8.dp))
        Text(
            "Secondary text",
            style = MaterialTheme.typography.titleSmall,
            modifier = Modifier.padding(top = 8.dp),
        )
        OutlinedTextField(
            value = secondary,
            onValueChange = { vm.onSecondaryChange(it) },
            modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
            placeholder = { Text("callsign / CQ text") },
            singleLine = true,
        )
    }
    if (showFreqDialog) {
        FrequencyDialog(
            onDismiss = { showFreqDialog = false },
            onSave = { name, freqHz ->
                vm.addFrequency(name, freqHz)
                showFreqDialog = false
            },
        )
    }
    editingFreq?.let { current ->
        FrequencyDialog(
            onDismiss = { editingFreq = null },
            onSave = { name, freqHz ->
                vm.updateFrequency(current.id, name, freqHz)
                editingFreq = null
            },
            initialName = current.name,
            initialFreqMhz = com.dominobrick.app.data.FavoriteFrequency.mhzInput(current.freqHz),
        )
    }
    renamingMac?.let { mac ->
        val currentName = devices.firstOrNull { it.mac == mac }?.name ?: ""
        var name by remember(mac) { mutableStateOf(currentName) }
        androidx.compose.material3.AlertDialog(
            onDismissRequest = { renamingMac = null },
            title = { Text("Name device") },
            text = {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    label = { Text("Name") },
                    placeholder = { Text(mac) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            },
            confirmButton = {
                androidx.compose.material3.TextButton(onClick = {
                    vm.renameDevice(mac, name)
                    renamingMac = null
                }) { Text("Save") }
            },
            dismissButton = {
                androidx.compose.material3.TextButton(onClick = { renamingMac = null }) {
                    Text("Cancel")
                }
            },
        )
    }
}

private fun statusText(status: DeviceStatus): String = when (status) {
    DeviceStatus.CONNECTED -> "connected"
    DeviceStatus.CONNECTING -> "connecting…"
    DeviceStatus.RETRYING -> "retrying…"
    DeviceStatus.IDLE -> "tap to rename"
}
