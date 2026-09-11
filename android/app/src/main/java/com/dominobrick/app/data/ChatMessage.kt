package com.dominobrick.app.data

enum class Direction { SENT, RECEIVED }

enum class Channel { PRIMARY, SECONDARY }

enum class SendStatus { QUEUED, AIRED }

data class ChatMessage(
    val id: Long,
    val direction: Direction,
    val channel: Channel,
    val text: String,
    val author: String? = null,
    val secondary: String? = null,
    val timestamp: Long,
    val airedCount: Int = 0,
) {
    val sendStatus: SendStatus
        get() = if (airedCount >= text.length) SendStatus.AIRED else SendStatus.QUEUED
}
