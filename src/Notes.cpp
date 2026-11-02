#include "Notes.h"

#if FREEINK_DEVICE_PAPERS3

#include <Arduino.h>
#include <Preferences.h>

#include <cstdio>
#include <cstring>

namespace weather {
namespace {
constexpr const char* kNamespace = "x4notes";
constexpr const char* kCountKey = "count";
}  // namespace

void clearNote(Note& note) { std::memset(note.text, 0, sizeof(note.text)); }

bool noteEmpty(const Note& note) { return note.text[0] == '\0'; }

bool loadNoteBook(NoteBook& out) {
  out.count = 0;
  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) return false;
  for (uint8_t i = 0; i < kNoteCount; ++i) {
    char key[4];
    std::snprintf(key, sizeof(key), "n%u", static_cast<unsigned>(i));
    if (!prefs.isKey(key)) continue;
    Note& note = out.notes[out.count];
    std::snprintf(note.text, sizeof(note.text), "%s", prefs.getString(key, "").c_str());
    if (!noteEmpty(note)) ++out.count;
  }
  prefs.end();
  return out.count > 0;
}

bool saveNoteBook(const NoteBook& book) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) return false;
  prefs.putUChar(kCountKey, book.count);
  for (uint8_t i = 0; i < kNoteCount; ++i) {
    char key[4];
    std::snprintf(key, sizeof(key), "n%u", static_cast<unsigned>(i));
    if (i < book.count) {
      prefs.putString(key, book.notes[i].text);
    } else if (prefs.isKey(key)) {
      prefs.remove(key);  // drop slots past the end, or a deleted note lingers
    }
  }
  prefs.end();
  return true;
}

void addNote(NoteBook& book) {
  const uint8_t last = book.count < kNoteCount ? book.count : static_cast<uint8_t>(kNoteCount - 1);
  for (uint8_t i = last; i > 0; --i) book.notes[i] = book.notes[i - 1];
  clearNote(book.notes[0]);
  if (book.count < kNoteCount) ++book.count;
}

void removeNote(NoteBook& book, uint8_t index) {
  if (index >= book.count) return;
  for (uint8_t i = index; i + 1 < book.count; ++i) book.notes[i] = book.notes[i + 1];
  --book.count;
  clearNote(book.notes[book.count]);
}

}  // namespace weather

#endif  // FREEINK_DEVICE_PAPERS3
