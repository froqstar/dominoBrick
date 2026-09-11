package com.dominobrick.app.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.dominobrick.app.ble.DominoBleClient
import com.dominobrick.app.data.ChatRepository
import com.dominobrick.app.data.DeviceStore
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch

class ChatViewModel(
    private val appContext: android.content.Context,
    val repo: ChatRepository,
    private val ble: DominoBleClient,
    private val store: DeviceStore,
) : ViewModel() {

    val messages = repo.messages
    val conn = ble.conn
    val draft = repo.draft

    private var flushJob: Job? = null

    init {
        if (store.loadMac() != null) {
            com.dominobrick.app.service.DominoLinkService.start(appContext)
        }
        viewModelScope.launch {
            var lastAired = 0
            repo.draft.map { list -> list.count { it.aired } }
                .collect { aired ->
                    if (aired > lastAired) pokeFlush()
                    lastAired = aired
                }
        }
        viewModelScope.launch {
            ble.conn.collect {
                if (it is DominoBleClient.Conn.Disconnected || it is DominoBleClient.Conn.Failed) {
                    repo.clearDraft()
                }
            }
        }
    }

    fun onDraftChange(newText: String) {
        val current = repo.draftText()
        if (newText == current) return
        if (newText.length > current.length && newText.startsWith(current)) {
            val added = newText.substring(current.length)
            repo.typeToDraft(added)
            viewModelScope.launch {
                runCatching { ble.sendBytes(added.toByteArray(Charsets.UTF_8)) }
            }
        }
    }

    fun insertAuthor(author: String?) {
        if (author.isNullOrEmpty() || author == "me") return
        val current = repo.draftText()
        val gap = if (current.isEmpty() || current.endsWith(" ")) "" else " "
        val text = "$gap$author "
        repo.typeToDraft(text)
        viewModelScope.launch {
            runCatching { ble.sendBytes(text.toByteArray(Charsets.UTF_8)) }
        }
    }

    fun clearHistory() {
        repo.clearHistory()
    }

    private fun pokeFlush() {
        flushJob?.cancel()
        flushJob = viewModelScope.launch {
            delay(3000)
            repo.flushDraft()
        }
    }
}
