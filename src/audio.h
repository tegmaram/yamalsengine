#pragma once

// Tiny procedural audio: thunder bursts only. No asset files, no rain bed.
struct Audio {
    bool init();
    void shutdown();
    void playThunder(float volume01);
    bool ok() const;
};
