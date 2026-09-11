package com.dominobrick.app.ui

import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import com.dominobrick.app.ble.DominoBleClient
import com.dominobrick.app.data.ChatMessage
import com.dominobrick.app.data.Direction
import com.dominobrick.app.data.SendStatus
import kotlinx.coroutines.delay

@Composable
fun ChatScreen(vm: ChatViewModel) {
    val messages by vm.messages.collectAsState()
    val draft by vm.draft.collectAsState()
    val listState = rememberLazyListState()
    val scheme = MaterialTheme.colorScheme
    var now by remember { mutableStateOf(System.currentTimeMillis()) }
    val lastMsg = messages.lastOrNull()
    LaunchedEffect(lastMsg?.id, lastMsg?.timestamp) {
        while (true) {
            now = System.currentTimeMillis()
            val last = messages.lastOrNull()
            if (last == null || last.direction != Direction.RECEIVED ||
                now - last.timestamp >= 1000
            ) {
                break
            }
            delay(200)
        }
    }

    LaunchedEffect(messages.size) {
        if (messages.isNotEmpty()) listState.animateScrollToItem(messages.size - 1)
    }
    val annotated = remember(draft, scheme) {
        buildAnnotatedString {
            draft.forEach {
                withStyle(SpanStyle(color = scheme.onSurface.copy(alpha = if (it.aired) 1f else 0.4f))) {
                    append(it.c)
                }
            }
        }
    }

    Column(Modifier.fillMaxSize().imePadding()) {
        LazyColumn(
            state = listState,
            modifier = Modifier.weight(1f).fillMaxWidth().padding(horizontal = 8.dp),
        ) {
            items(messages, key = { it.id }) {
                val active = it.direction == Direction.RECEIVED && now - it.timestamp < 1000
                MessageRow(it, active) { vm.insertAuthor(it.author) }
            }
        }
        TextField(
            value = TextFieldValue(annotated, TextRange(annotated.length)),
            onValueChange = { vm.onDraftChange(it.text) },
            modifier = Modifier.fillMaxWidth().padding(8.dp),
            placeholder = { Text("Type…") },
            singleLine = false,
            maxLines = 4,
            keyboardOptions = KeyboardOptions(
                keyboardType = KeyboardType.Text,
                autoCorrectEnabled = false,
            ),
        )
    }
}

@Composable
private fun MessageRow(msg: ChatMessage, active: Boolean, onTap: () -> Unit) {
    val scheme = MaterialTheme.colorScheme
    val mine = msg.direction == Direction.SENT
    val queued = mine && msg.sendStatus == SendStatus.QUEUED
    val bubble = if (mine) scheme.primaryContainer else scheme.surfaceContainerHigh
    val pulse = if (active) {
        val transition = rememberInfiniteTransition(label = "rx")
        transition.animateFloat(
            initialValue = 1f,
            targetValue = 0.55f,
            animationSpec = infiniteRepeatable(tween(500), RepeatMode.Reverse),
            label = "rxPulse",
        )
    } else {
        remember { mutableStateOf(1f) }
    }
    Row(
        Modifier.fillMaxWidth().padding(vertical = 2.dp),
        horizontalArrangement = if (mine) Arrangement.End else Arrangement.Start,
    ) {
        Surface(
            shape = RoundedCornerShape(16.dp),
            color = bubble,
            modifier = Modifier.widthIn(max = 280.dp).clickable(onClick = onTap)
                .graphicsLayer { alpha = pulse.value },
        ) {
            Column(Modifier.padding(horizontal = 12.dp, vertical = 8.dp)) {
                if (!mine) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = msg.author ?: "peer",
                            style = MaterialTheme.typography.labelSmall,
                            color = scheme.onSurfaceVariant,
                        )
                        msg.secondary?.let {
                            Text(
                                text = " · $it",
                                style = MaterialTheme.typography.labelSmall,
                                color = scheme.onSurfaceVariant.copy(alpha = 0.6f),
                                maxLines = 1,
                            )
                        }
                    }
                }
                Text(
                    text = msg.text,
                    color = if (mine) scheme.onPrimaryContainer else scheme.onSurface,
                    modifier = if (queued) Modifier.alpha(0.6f) else Modifier,
                )
            }
        }
    }
}

internal fun connLabel(conn: DominoBleClient.Conn): String = when (conn) {
    is DominoBleClient.Conn.Disconnected -> "disconnected"
    is DominoBleClient.Conn.Connecting -> "connecting…"
    is DominoBleClient.Conn.Connected -> "connected"
    is DominoBleClient.Conn.Failed -> "failed: ${conn.reason}"
}
