// effect.c — On-disk effect table implementation for Project 4.
//
// Shared infrastructure for idempotent filesystem side-effects.
// The effect table is an on-disk open-addressing hash table with linear
// probing, stored in reserved metadata blocks allocated by mkfs.
//
// Key 0 is reserved as the empty slot sentinel.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "stat.h"
#include "file.h"
#include "mmu.h"
#include "proc.h"
#include "effect.h"

static struct sleeplock keylocks[NKEYLOCKS];
static uint effect_start = 0;
static uint neffect = 0;
static int effect_dev = ROOTDEV;

void
effectinit(int dev)
{
  struct superblock esb;
  int i;

  for(i = 0; i < NKEYLOCKS; i++){
    initsleeplock(&keylocks[i], "effect_key");
  }

  readsb(dev, &esb);
  effect_start = esb.effect_start;
  neffect = esb.neffect;
  effect_dev = dev;

  cprintf("effect: initialized (start block %d, %d blocks, %d slots)\n",
          effect_start, neffect, EFFECT_NSLOTS);
}

void
key_lock(uint64 key)
{
  int idx = (int)(key % NKEYLOCKS);
  acquiresleep(&keylocks[idx]);
}

void
key_unlock(uint64 key)
{
  int idx = (int)(key % NKEYLOCKS);
  releasesleep(&keylocks[idx]);
}

// Search on-disk table for key.
// Returns 0 if found and stores the cached result in *res.
// Returns -1 if not found.
int
effect_lookup(uint64 key, int *res)
{
  uint p, slot, blockno, idx;
  struct buf *bp;
  struct effect_entry *entries;

  if(key == 0 || effect_start == 0)
    return -1;

  uint start_slot = (uint)(key % EFFECT_NSLOTS);

  for(p = 0; p < EFFECT_NSLOTS; p++){
    slot = (start_slot + p) % EFFECT_NSLOTS;
    blockno = effect_start + (slot / EFFECT_EPB);
    idx = slot % EFFECT_EPB;

    bp = bread(effect_dev, blockno);
    entries = (struct effect_entry*)bp->data;

    if(entries[idx].key == key){
      *res = entries[idx].result;
      brelse(bp);
      return 0; // Found cached result
    }

    if(entries[idx].key == 0){
      // Reached an empty slot: key does not exist in the table
      brelse(bp);
      return -1;
    }

    brelse(bp);
  }

  return -1; // Scanned entire table without match
}

// Insert or update an entry in the on-disk effect table.
// Must be called inside an active filesystem transaction (begin_op / end_op).
// Returns 0 on success.
// Returns -1 on full table (never panics, logs a single kernel line).
int
effect_insert(uint64 key, int res)
{
  uint p, slot, blockno, idx;
  struct buf *bp;
  struct effect_entry *entries;

  if(key == 0 || effect_start == 0)
    return -1;

  uint start_slot = (uint)(key % EFFECT_NSLOTS);

  for(p = 0; p < EFFECT_NSLOTS; p++){
    slot = (start_slot + p) % EFFECT_NSLOTS;
    blockno = effect_start + (slot / EFFECT_EPB);
    idx = slot % EFFECT_EPB;

    bp = bread(effect_dev, blockno);
    entries = (struct effect_entry*)bp->data;

    if(entries[idx].key == 0 || entries[idx].key == key){
      entries[idx].key = key;
      entries[idx].result = res;
      entries[idx].pad = 0;
      log_write(bp);
      brelse(bp);
      return 0; // Success
    }

    brelse(bp);
  }

  // All probe slots exhausted: table is full
  cprintf("effect: table full, key=%x%x\n", (uint)(key >> 32), (uint)key);
  return -1;
}

// Perform the underlying filesystem operation inside an existing transaction.
// No begin_op() / end_op() calls here, since xv6 transactions are not reentrant.
// Caps payload length n <= EFFECT_MAXDATA to fit inside MAXOPBLOCKS.
int
do_effect(int op, char *path, char *buf, int n)
{
  struct inode *ip;

  if(op == EFFECT_OP_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0)
      return -1;
    iunlockput(ip);
    return 0;
  } else if(op == EFFECT_OP_APPEND){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0)
      return -1;
    int r = 0;
    if(n > 0){
      r = writei(ip, buf, ip->size, n);
    }
    iunlockput(ip);
    return r;
  }
  return -1;
}

// sys_effect(key, op, path, buf, n)
// Explicit caller-supplied idempotency key (64-bit).
int
sys_effect(void)
{
  int key_lo, key_hi;
  int op, n, res;
  char *path;
  char *ubuf = 0;

  // On 32-bit x86, a 64-bit key occupies two 32-bit argument slots
  if(argint(0, &key_lo) < 0 || argint(1, &key_hi) < 0)
    return -1;
  uint64 key = ((uint64)(uint)key_hi << 32) | (uint)key_lo;

  // Fix #2: Reserve key == 0 as invalid input
  if(key == 0){
    cprintf("effect: invalid key 0\n");
    return -1;
  }

  if(argint(2, &op) < 0)
    return -1;
  if(argstr(3, &path) < 0)
    return -1;
  if(argint(5, &n) < 0)
    return -1;

  if(op != EFFECT_OP_CREATE && op != EFFECT_OP_APPEND)
    return -1;

  if(n < 0 || n > EFFECT_MAXDATA)
    return -1;

  if(op == EFFECT_OP_APPEND && n > 0){
    if(argptr(4, &ubuf, n) < 0)
      return -1;
  }

  // Acquire in-memory lock for this key to prevent concurrent duplicate execution
  key_lock(key);

  begin_op();

  if(effect_lookup(key, &res) == 0){
    // Key already completed: return stored result without repeating side effect
    end_op();
    key_unlock(key);
    return res;
  }

  // Execute the side effect
  res = do_effect(op, path, ubuf, n);

  // Record key and result in the on-disk effect table (same transaction)
  if(effect_insert(key, res) < 0){
    // Table is full (Fix #3): abort and return -1 cleanly
    end_op();
    key_unlock(key);
    return -1;
  }

  end_op();
  key_unlock(key);
  return res;
}

