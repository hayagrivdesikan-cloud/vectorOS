// effect.h: On-disk effect table for exactly-once syscalls across crashes.
//
// The effect table is a fixed-size, open-addressed hash table stored in
// reserved blocks on disk.  Each entry records the (key, result) of a
// completed filesystem side-effect so that replaying the same key after
// a crash returns the cached result without re-executing the operation.
//
// Used by both sys_effect (caller-supplied key) and sys_effect_smart
// (kernel-derived replay-window key).

#ifndef _EFFECT_H_
#define _EFFECT_H_

// ── Disk layout constants ──────────────────────────────────────────────
//
// BSIZE on this x86 tree is 512.  Each effect_entry is 16 bytes,
// so one block holds 512 / 16 = 32 entries.  With 4 reserved blocks
// the table has 128 slots total.

#ifndef BSIZE
#define BSIZE 512
#endif

#define NEFFECTBLOCKS   4                          // blocks reserved on disk
#define EFFECT_EPB      (BSIZE / sizeof(struct effect_entry))  // entries per block (32)
#define EFFECT_NSLOTS   (NEFFECTBLOCKS * EFFECT_EPB)           // total slots   (128)

// ── On-disk entry ──────────────────────────────────────────────────────
//
// key == 0 means the slot is free (sentinel).  Callers of sys_effect
// must never pass key == 0; generate_kernel_key() maps 0 → 1 if it
// ever produces it.

struct effect_entry {
  uint64 key;     // idempotency key (0 = free slot)
  int    result;  // cached return value of the operation
  uint   pad;     // pad to 16 bytes for clean block alignment
};

// ── Operation codes (shared between kernel and user space) ─────────────

#define EFFECT_OP_CREATE  1   // create a file (path); buf/n ignored
#define EFFECT_OP_APPEND  2   // append buf[0..n-1] to file at path

// ── Per-key spinlocks ──────────────────────────────────────────────────
//
// A small array of spinlocks striped by (key % NKEYLOCKS) serialises
// concurrent callers that happen to hash to the same table region.

#define NKEYLOCKS  16

// ── Payload cap ────────────────────────────────────────────────────────
//
// The maximum number of bytes a single effect() APPEND may write.
// Chosen so that one effect transaction fits inside MAXOPBLOCKS (10):
//
//   effect-table block   1
//   file inode block     1
//   bitmap block         1       (data block allocation)
//   data block(s)        1       (one block of payload)
//   indirect block       1       (worst case)
//   dir inode + data     2       (CREATE only)
//   ────────────────────────
//   total worst case     7   (< MAXOPBLOCKS = 10)
//
// Capping at 512 bytes (one disk block) keeps a comfortable margin.

#define EFFECT_MAXDATA  512

// ── Replay-window constants (sys_effect_smart) ─────────────────────────
//
// The kernel-derived key incorporates (tick / WINDOW_TICKS) so that
// identical payloads in different time windows produce different keys.

#define WINDOW_TICKS  100   // ~1 second at the default 100-Hz tick rate

// ── FNV-1a parameters (64-bit) ─────────────────────────────────────────

#define FNV_OFFSET_BASIS  0xcbf29ce484222325ULL
#define FNV_PRIME         0x100000001b3ULL

#endif // _EFFECT_H_
