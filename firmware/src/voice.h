#pragma once

// Voice chat screen. Connects to Wi-Fi and the server's voice WebSocket, and shows an animated face
// while you talk. BOOT switches personality. Returns when KEY is pressed or after
// VOICE_IDLE_SECONDS without conversation.
void runVoice();
