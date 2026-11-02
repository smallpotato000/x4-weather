#pragma once

#include <stdint.h>

namespace weather {

// A tiny memo pad: a few short notes kept in NVS. Editing happens in the
// settings UI (which owns the keyboard); the data pages show them read-only.
static constexpr uint8_t kNoteCount = 8;
static constexpr uint8_t kNoteLength = 96;

struct Note {
  char text[kNoteLength + 1] = {0};
};

struct NoteBook {
  uint8_t count = 0;  // used slots, newest first
  Note notes[kNoteCount];
};

void clearNote(Note& note);
bool noteEmpty(const Note& note);

// Loads the pad from NVS ("x4notes"); false when there is nothing stored yet.
bool loadNoteBook(NoteBook& out);
bool saveNoteBook(const NoteBook& book);

// Inserts a blank note at the front (dropping the oldest when full).
void addNote(NoteBook& book);
// Removes one slot, keeping the order.
void removeNote(NoteBook& book, uint8_t index);

}  // namespace weather
