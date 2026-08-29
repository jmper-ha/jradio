#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "file_browser.h"

/* The one directory listing on the device, shared by every volume.
 *
 * One directory is on screen at a time, so one listing is enough - and it has
 * to be one, because at 264 bytes an entry a second copy would be another
 * 68 KB. Reading a directory refills it in place, so a read invalidates
 * whatever the previous one held, whichever volume that came from.
 *
 * Which volume a path is on is carried by the path itself: /usb0/... or
 * /sd0/.... Nothing here knows the difference, which is the point - the USB
 * drive and the SD card differ in how they are mounted and in nothing else. */

// Allocates the listing and its lock. Idempotent: every volume calls it as it
// comes up, and the first call is the one that does the work.
esp_err_t file_storage_init(void);

/* Tells the listing that `root` is mountable, and how to ask whether it is
 * mounted right now.
 *
 * Reading a directory on a volume that is not mounted has to fail as "not
 * mounted" rather than as a missing directory: a file player deciding whether
 * to open a track needs the difference, and so does the screen. Registered by
 * the volume rather than listed here, so this file needs no idea of what a USB
 * drive or a card is. */
typedef bool (*file_storage_mounted_fn)(void);
esp_err_t file_storage_register_volume(const char *root, file_storage_mounted_fn mounted);

// True when `path` lies on a registered volume that is mounted right now.
// False for an unmounted volume, and for a path on no volume at all.
bool file_storage_path_mounted(const char *path);

// Reads `path` into the shared listing, replacing what was there.
esp_err_t file_storage_read_directory(const char *path);

/* Reads the tracks of the .m3u or .pls file at `path` into the shared listing.
 *
 * A playlist takes the place of a directory rather than sitting beside one:
 * the listing is what the browser shows, what track advance steps through, and
 * what the previous/next keys move in, so a playlist that is open *is* the
 * current directory as far as everything above here is concerned. Browsing up
 * out of it lands in the folder the file is in, because that is the parent of
 * its path.
 *
 * The order in the file is kept - it is the playing order - so unlike a
 * directory this listing is never sorted. Entries the device cannot open are
 * counted, not listed: see file_browser_dir_t::dropped_unplayable. */
esp_err_t file_storage_read_playlist(const char *path);

// Whichever of the two `path` names. Used wherever a path comes back from
// somewhere that stored it - a resume point, the track being played - and the
// code that opens it should not have to re-derive which kind it is.
esp_err_t file_storage_open(const char *path);

/* True while the listing is showing `root` or something inside it.
 *
 * Asked by a volume on its way out, because with two of them the listing can
 * be on the other one - browsing the card while the drive is pulled - and
 * emptying that would blank a browser whose files are all still there. */
bool file_storage_listing_is_on(const char *root);

// Points the listing at `root` with no entries in it. Used when a volume goes
// away, and when a source is selected whose root will not open: the browser
// polls the listing independently of any mount, and it must not be left
// showing another volume's files under this one's name.
void file_storage_open_empty(const char *root);

// Accessors copy out under the listing lock: a volume can be unmounted, and
// the listing cleared, while the UI task is walking it.
size_t file_storage_entry_count(void);
bool file_storage_entry_at(size_t index, file_browser_entry_t *out);

/* Copies one row of the listing together with the full path that row opens,
 * both taken under a single hold of the listing lock.
 *
 * The two used to be fetched by separate calls, and a listing replaced in
 * between joined a name from the directory that was open to the path of the
 * one that had just replaced it. The player then opened a file that never
 * existed, reported the track as failed, and the screen fell back to the
 * browser - which reads as the browser reopening itself. */
bool file_storage_entry_path(size_t index, file_browser_entry_t *entry, char *path,
                             size_t capacity);

// Index of `name` in the current listing, or the entry count when it is absent
// or unreadable. Used to find a remembered track after a restart.
size_t file_storage_find_entry(const char *name);
bool file_storage_current_path(char *out, size_t out_size);

// Index of the next playable file at or after `from`. Returns a value past the
// end of the listing when there is none left *and* when the listing could not
// be read, so a caller that stops on "past the end" stops in both cases. Never
// returns a valid index it is not sure about - answering 0 on failure would
// send track advance back to the first file. Used to advance to the next
// track.
size_t file_storage_next_file(size_t from);

// The other direction, for the previous-track key: the last playable file
// before `before`, and the same "past the end" answer when there is none.
size_t file_storage_previous_file(size_t before);

// Prints the listing, one line per entry. Called after a volume is mounted, so
// the boot log says what the device can see.
void file_storage_log_listing(void);
