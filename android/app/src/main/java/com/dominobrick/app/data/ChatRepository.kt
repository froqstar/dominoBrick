package com.dominobrick.app.data

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class ChatRepository(private val externalScope: CoroutineScope) {

    private val _messages = MutableStateFlow<List<ChatMessage>>(emptyList())
    val messages: StateFlow<List<ChatMessage>> = _messages.asStateFlow()

    private val _lastSecondaryText = MutableStateFlow("")
    val lastSecondaryText: StateFlow<String> = _lastSecondaryText.asStateFlow()

    private var nextId = 1L
    private val secondaryBuffer = StringBuilder()
    private val window = ArrayDeque<Char>()

    private val callsignPattern = Regex("(?i)\\b([A-Z0-9]{1,4}[0-9][A-Z0-9/]{1,7})\\b")

    data class DraftChar(val c: Char, val aired: Boolean)

    private val _draft = MutableStateFlow<List<DraftChar>>(emptyList())
    val draft: StateFlow<List<DraftChar>> = _draft.asStateFlow()

    fun attach(ble: com.dominobrick.app.ble.DominoBleClient) {
        externalScope.launch { ble.rxPrimary.collect { addReceived(Channel.PRIMARY, it) } }
        externalScope.launch { ble.rxSecondary.collect { addReceived(Channel.SECONDARY, it) } }
        externalScope.launch { ble.txProgress.collect { onEcho(it) } }
    }

    @Synchronized
    fun draftText(): String = _draft.value.joinToString("") { it.c.toString() }

    @Synchronized
    fun typeToDraft(text: String) {
        if (text.isEmpty()) return
        _draft.value = _draft.value + text.map { DraftChar(it, false) }
    }

    @Synchronized
    fun clearDraft() {
        _draft.value = emptyList()
    }

    @Synchronized
    fun flushDraft() {
        val current = _draft.value
        if (current.isEmpty()) return
        val text = current.joinToString("") { it.c.toString() }
        val aired = current.takeWhile { it.aired }.size
        _draft.value = emptyList()
        append(
            ChatMessage(nextId++, Direction.SENT, Channel.PRIMARY, text, "me", null,
                System.currentTimeMillis(), aired),
        )
    }

    @Synchronized
    fun onEcho(c: Char) {
        val draft = _draft.value
        val didx = draft.indexOfFirst { !it.aired && it.c == c }
        if (didx >= 0) {
            _draft.value = draft.toMutableList().also { it[didx] = it[didx].copy(aired = true) }
            return
        }
        val current = _messages.value
        val idx = current.indexOfFirst {
            it.direction == Direction.SENT && it.airedCount < it.text.length && it.text[it.airedCount] == c
        }
        if (idx < 0) return
        val msg = current[idx]
        updateAt(idx, msg.copy(airedCount = msg.airedCount + 1))
    }

    @Synchronized
    fun addReceived(channel: Channel, c: Char) {
        val now = System.currentTimeMillis()
        if (channel == Channel.SECONDARY) {
            secondaryBuffer.append(c)
            if (secondaryBuffer.length > 256) secondaryBuffer.delete(0, secondaryBuffer.length - 256)
            _lastSecondaryText.value = secondaryBuffer.toString()
            window.addLast(c)
            if (window.size > 32) window.removeFirst()
            refreshSecondary()
            return
        }
        val current = _messages.value
        val last = current.lastOrNull()
        if (last != null && last.direction == Direction.RECEIVED && last.channel == channel &&
            now - last.timestamp < 3000
        ) {
            updateLast(last.copy(text = last.text + c, timestamp = now, author = last.author ?: extractCallsign()))
        } else {
            window.clear()
            append(ChatMessage(nextId++, Direction.RECEIVED, channel, c.toString(), extractCallsign(), null, now))
        }
    }

    @Synchronized
    fun refreshSecondary() {
        val call = extractCallsign()
        val text = window.joinToString("")
        val current = _messages.value
        val last = current.lastOrNull() ?: return
        val now = System.currentTimeMillis()
        if (last.direction == Direction.RECEIVED && last.channel == Channel.PRIMARY &&
            now - last.timestamp < 3000 && (last.author != call || last.secondary != text)
        ) {
            updateLast(last.copy(author = call ?: last.author, secondary = text.ifEmpty { null }, timestamp = last.timestamp))
        }
    }

    fun extractCallsign(): String? {
        return callsignPattern.findAll(window.joinToString(""))
            .map { it.groupValues[1].uppercase().trim('/') }
            .filter { it.length in 3..12 && it.any(Char::isLetter) && it.any(Char::isDigit) }
            .maxByOrNull { it.length }
    }

    fun secondarySnapshot(): String? {
        val s = secondaryBuffer.toString().trim()
        return s.ifEmpty { null }
    }

    @Synchronized
    fun clearHistory() {
        _messages.value = emptyList()
    }

    private fun append(msg: ChatMessage) {
        _messages.value = _messages.value + msg
    }

    private fun updateLast(msg: ChatMessage) {
        val current = _messages.value
        _messages.value = current.dropLast(1) + msg
    }

    private fun updateAt(idx: Int, msg: ChatMessage) {
        val current = _messages.value.toMutableList()
        current[idx] = msg
        _messages.value = current
    }
}
