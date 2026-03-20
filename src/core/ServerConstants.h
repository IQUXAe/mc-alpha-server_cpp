#pragma once

#include <cstdint>

// Server configuration constants
namespace ServerConstants {
    // Tick timing
    constexpr int TICKS_PER_SECOND = 20;
    constexpr int MILLISECONDS_PER_TICK = 50;
    
    // View distance
    constexpr int VIEW_DISTANCE_MIN = 3;
    constexpr int VIEW_DISTANCE_MAX = 15;
    constexpr int VIEW_DISTANCE_DEFAULT = 10;
    
    // Chunk loading
    constexpr int CHUNKS_PER_TICK = 15;
    constexpr int CHUNK_GENERATION_EXTRA_RADIUS = 2;
    
    // Network
    constexpr int MAX_PACKET_SIZE = 65536;
    constexpr int SEND_QUEUE_MAX_BYTES = 1048576;  // 1 MB
    constexpr int READ_TIMEOUT_TICKS = 1200;  // 60 seconds
    constexpr int KEEPALIVE_INTERVAL_TICKS = 20;  // 1 second
    
    // Chunk data sending
    constexpr int CHUNK_DATA_SEND_DELAY_TICKS = 50;
    
    // Player
    constexpr float PLAYER_EYE_HEIGHT = 1.62f;
    constexpr float PLAYER_WIDTH = 0.6f;
    constexpr float PLAYER_HEIGHT = 1.8f;
    
    // Entity tracking
    constexpr double TRACKING_DISTANCE = 64.0;  // Blocks
    constexpr int ENTITY_TRACKING_INTERVAL_TICKS = 3;
    
    // Physics
    constexpr double GRAVITY = 0.08;
    constexpr double TERMINAL_VELOCITY = 78.4;
    constexpr double FRICTION = 0.91;
    
    // World
    constexpr int CHUNK_SIZE_X = 16;
    constexpr int CHUNK_SIZE_Y = 128;
    constexpr int CHUNK_SIZE_Z = 16;
    constexpr int SPAWN_Y_DEFAULT = 64;
    
    // Auto-save
    constexpr int AUTO_SAVE_INTERVAL_TICKS_DEFAULT = 6000;  // 5 minutes
    
    // Commands
    constexpr int MAX_CHAT_MESSAGE_LENGTH = 100;
}
