package com.dominobrick.app.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.dominobrick.app.data.FavoriteFrequency

@Composable
fun FrequencyDialog(
    onDismiss: () -> Unit,
    onSave: (String, Long) -> Unit,
    initialName: String = "",
    initialFreqMhz: String = "",
) {
    var name by remember { mutableStateOf(initialName) }
    var freq by remember { mutableStateOf(initialFreqMhz) }
    var error by remember { mutableStateOf<String?>(null) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Add frequency") },
        text = {
            Column {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    label = { Text("Name") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = freq,
                    onValueChange = { freq = it },
                    label = { Text("Frequency (MHz)") },
                    placeholder = { Text("14.074") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                )
                error?.let {
                    Text(it, color = MaterialTheme.colorScheme.error)
                }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                val freqHz = FavoriteFrequency.parseMhz(freq)
                if (name.isBlank()) {
                    error = "name required"
                } else if (freqHz == null) {
                    error = "enter MHz, e.g. 14.074"
                } else {
                    onSave(name, freqHz)
                }
            }) { Text("Save") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}
