#pragma once

// Tiny procedural audio: thunder, rain bed and stereo relay cue. No asset files.
struct Audio {
    bool init();
    void ambience(float rain,float relay,float pan);
    void shutdown();
    void playThunder(float volume01);
    bool ok() const;
};
