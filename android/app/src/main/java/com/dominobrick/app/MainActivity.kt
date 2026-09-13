package com.dominobrick.app

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import com.dominobrick.app.ui.AppTheme
import com.dominobrick.app.ui.ChatScreen
import com.dominobrick.app.ui.FreqViewModel
import com.dominobrick.app.ui.FrequencyDialog
import com.dominobrick.app.ui.connLabel
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import com.dominobrick.app.ui.ChatViewModel
import com.dominobrick.app.ui.SettingsScreen
import com.dominobrick.app.ui.SettingsViewModel

class MainActivity : ComponentActivity() {

    private val chatVm: ChatViewModel by viewModels {
        graphFactory { app -> ChatViewModel(app, app.repo, app.ble, app.store) }
    }
    private val settingsVm: SettingsViewModel by viewModels {
        graphFactory { app -> SettingsViewModel(app, app.ble, app.store, app.freqs, app.repo) }
    }
    private val freqVm: FreqViewModel by viewModels {
        graphFactory { app -> FreqViewModel(app.ble, app.freqs) }
    }

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            permissionLauncher.launch(Manifest.permission.BLUETOOTH_CONNECT)
        }
        setContent {
            AppTheme {
                var screen by remember { mutableStateOf(Screen.Chat) }
                var menu by remember { mutableStateOf(false) }
                var demo by remember { mutableStateOf(false) }
                val conn by chatVm.conn.collectAsState()
                val freqHz by freqVm.freqHz.collectAsState()
                val favorites by freqVm.favorites.collectAsState()
                val freqMatch = freqHz?.let { hz -> favorites.firstOrNull { it.freqHz == hz } }
                var showFreqSave by remember { mutableStateOf(false) }
                val app = application as DominoApp
                val names by app.store.names.collectAsState()
                val currentAddr by app.ble.currentAddress.collectAsState()
                val connectedName = currentAddr?.let { addr ->
                    names.entries.firstOrNull { it.key.equals(addr, ignoreCase = true) }?.value
                }
                val demoScope = rememberCoroutineScope()
                var demoJob by remember { mutableStateOf<Job?>(null) }
                BackHandler(enabled = screen == Screen.Settings) { screen = Screen.Chat }
                Column(Modifier.fillMaxSize()) {
                    TopBar(
                        title = if (screen == Screen.Chat) (connectedName ?: "dominoBrick") else "Settings",
                        subtitle = connLabel(conn),
                        showBack = screen == Screen.Settings,
                        onBack = { screen = Screen.Chat },
                        menuOpen = menu,
                        onMenuToggle = { menu = !menu },
                        onPick = { screen = it; menu = false },
                        onClearHistory = {
                            menu = false
                            chatVm.clearHistory()
                        },
                        showFreq = screen == Screen.Chat,
                        freqHz = freqHz,
                        freqMatchName = freqMatch?.name,
                        favorites = favorites,
                        showFreqPlus = freqHz != null && freqMatch == null,
                        onSelectFreq = { freqVm.select(it) },
                        onAddFreq = { showFreqSave = true },
                        demoOn = demo,
                        onToggleDemo = {
                            demo = !demo
                            menu = false
                            if (demo) {
                                demoJob = demoScope.launch {
                                    com.dominobrick.app.demo.DemoFeed.run(app.repo, app.ble)
                                }
                            } else {
                                demoJob?.cancel()
                                demoJob = null
                            }
                        },
                    )
                    when (screen) {
                        Screen.Chat -> ChatScreen(chatVm)
                        Screen.Settings -> SettingsScreen(settingsVm)
                    }
                    if (showFreqSave && freqHz != null) {
                        FrequencyDialog(
                            onDismiss = { showFreqSave = false },
                            onSave = { name, hz ->
                                freqVm.saveFavorite(name, hz)
                                showFreqSave = false
                            },
                            initialFreqMhz = com.dominobrick.app.data.FavoriteFrequency.mhzInput(freqHz!!),
                        )
                    }
                }
            }
        }
    }

    private fun <T : ViewModel> graphFactory(make: (DominoApp) -> T): ViewModelProvider.Factory {
        val app = application as DominoApp
        return object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <M : ViewModel> create(modelClass: Class<M>): M = make(app) as M
        }
    }

    private enum class Screen { Chat, Settings }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    private fun TopBar(
        title: String,
        subtitle: String,
        showBack: Boolean,
        onBack: () -> Unit,
        menuOpen: Boolean,
        onMenuToggle: () -> Unit,
        onPick: (Screen) -> Unit,
        onClearHistory: () -> Unit,
        demoOn: Boolean,
        onToggleDemo: () -> Unit,
        showFreq: Boolean,
        freqHz: Long?,
        freqMatchName: String?,
        favorites: List<com.dominobrick.app.data.FavoriteFrequency>,
        showFreqPlus: Boolean,
        onSelectFreq: (Long) -> Unit,
        onAddFreq: () -> Unit,
    ) {
        var freqMenu by remember { mutableStateOf(false) }
        TopAppBar(
            title = {
                Column {
                    Text(title)
                    Text(
                        subtitle,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            },
            navigationIcon = {
                if (showBack) {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                }
            },
            actions = {
                if (showFreq && freqHz != null) {
                    TextButton(onClick = { freqMenu = true }) {
                        Column(horizontalAlignment = Alignment.End) {
                            freqMatchName?.let {
                                Text(
                                    it,
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                            }
                            Text(
                                com.dominobrick.app.data.FavoriteFrequency.mhzInput(freqHz) + " MHz",
                                style = MaterialTheme.typography.bodyMedium,
                            )
                        }
                    }
                    DropdownMenu(expanded = freqMenu, onDismissRequest = { freqMenu = false }) {
                        favorites.forEach { fav ->
                            DropdownMenuItem(
                                text = {
                                    Column {
                                        Text(fav.name)
                                        Text(
                                            fav.display(),
                                            style = MaterialTheme.typography.labelSmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        )
                                    }
                                },
                                onClick = {
                                    freqMenu = false
                                    onSelectFreq(fav.freqHz)
                                },
                            )
                        }
                    }
                }
                if (showFreq && showFreqPlus) {
                    IconButton(onClick = onAddFreq) {
                        Icon(Icons.Filled.Add, contentDescription = "Save frequency")
                    }
                }
                IconButton(onClick = onMenuToggle) {
                    Icon(Icons.Filled.MoreVert, contentDescription = "Menu")
                }
                DropdownMenu(expanded = menuOpen, onDismissRequest = onMenuToggle) {
                    DropdownMenuItem(
                        text = { Text("Settings") },
                        onClick = { onPick(Screen.Settings) },
                    )
                    DropdownMenuItem(
                        text = { Text(if (demoOn) "Stop demo" else "Demo mode") },
                        onClick = onToggleDemo,
                    )
                    DropdownMenuItem(
                        text = { Text("Clear history") },
                        onClick = onClearHistory,
                    )
                }
            },
        )
    }
}
