// effecttest.c — User space test program for sys_effect and sys_crashat.

#include "types.h"
#include "stat.h"
#include "user.h"
#include "effect.h"

static void
check_file_size(const char *path, uint expected_size)
{
  struct stat st;
  int fd = open(path, 0);
  if(fd < 0){
    printf(1, "FAIL: cannot open %s\n", path);
    exit();
  }
  if(fstat(fd, &st) < 0){
    printf(1, "FAIL: fstat failed\n");
    close(fd);
    exit();
  }
  close(fd);
  if(st.size != expected_size){
    printf(1, "FAIL: %s size %d, expected %d\n", path, st.size, expected_size);
    exit();
  }
}

int
main(int argc, char *argv[])
{
  printf(1, "=== Running effecttest ===\n");

  // Checkpoint 8: Initialize clean file for crash testing
  if(argc >= 2 && strcmp(argv[1], "initcrash") == 0){
    unlink("crashfile.txt");
    int r = effect(100, EFFECT_OP_CREATE, "crashfile.txt", 0, 0);
    if(r == 0)
      printf(1, "INITCRASH_DONE\n");
    else
      printf(1, "INITCRASH_FAIL\n");
    exit();
  }

  // Checkpoint 5 & 8: Crash test mode if invoked with "crash N [key] [file]"
  if(argc >= 3 && strcmp(argv[1], "crash") == 0){
    int cp = atoi(argv[2]);
    uint64 key = 9000 + (uint64)cp;
    char fname[32];
    strcpy(fname, "cfile.txt");
    if(argc >= 4) key = (uint64)atoi(argv[3]);
    if(argc >= 5) strcpy(fname, argv[4]);
    printf(1, "Testing crashat(%d) key=%d file=%s...\n", cp, (int)key, fname);
    crashat(cp);
    int r = effect(key, EFFECT_OP_APPEND, fname, "EXACTLY_ONCE!", 13);
    printf(1, "CRASH_NOT_TRIGGERED res=%d\n", r);
    exit();
  }

  // Checkpoint 8: Check file size after reboot before replay
  if(argc >= 2 && strcmp(argv[1], "stat") == 0){
    char fname[32];
    strcpy(fname, "cfile.txt");
    if(argc >= 3) strcpy(fname, argv[2]);
    struct stat st;
    int fd = open(fname, 0);
    if(fd < 0 || fstat(fd, &st) < 0){
      printf(1, "STAT_RESULT: size=0 (not created)\n");
    } else {
      printf(1, "STAT_RESULT: size=%d\n", st.size);
      close(fd);
    }
    exit();
  }

  // Checkpoint 8: Replay identical request after reboot: "replay [key] [file]"
  if(argc >= 2 && strcmp(argv[1], "replay") == 0){
    uint64 key = 9000;
    char fname[32];
    strcpy(fname, "cfile.txt");
    if(argc >= 3) key = (uint64)atoi(argv[2]);
    if(argc >= 4) strcpy(fname, argv[3]);
    printf(1, "Replaying request key=%d file=%s...\n", (int)key, fname);
    int r = effect(key, EFFECT_OP_APPEND, fname, "EXACTLY_ONCE!", 13);
    struct stat st;
    int fd = open(fname, 0);
    if(fd < 0 || fstat(fd, &st) < 0){
      printf(1, "REPLAY_FAIL\n");
      exit();
    }
    close(fd);
    printf(1, "REPLAY_RESULT: size=%d res=%d\n", st.size, r);
    exit();
  }

  // Checkpoint 7: Table-full test mode if invoked with "full"
  if(argc >= 2 && strcmp(argv[1], "full") == 0){
    printf(1, "Testing table-full path (filling all %d slots)...\n", EFFECT_NSLOTS);
    int full_detected = 0;
    for(int k = 1; k <= EFFECT_NSLOTS + 5; k++){
      uint64 key = 8000 + k;
      int rf = effect(key, EFFECT_OP_CREATE, "f_full", 0, 0);
      if(rf == -1){
        full_detected = 1;
        printf(1, "   Detected table-full at key %d (returned -1 cleanly)\n", (int)key);
        break;
      }
    }
    if(!full_detected){
      printf(1, "FAIL: table never filled after %d insertions\n", EFFECT_NSLOTS + 5);
      exit();
    }
    printf(1, "   PASS: table-full returned -1 cleanly without panicking!\n");
    printf(1, "=== ALL effecttest full tests PASSED ===\n");
    exit();
  }

  // 1. Test key == 0 rejection (Fix #2)
  printf(1, "1. Testing key == 0 rejection...\n");
  int r0 = effect(0, EFFECT_OP_CREATE, "zero_test.txt", 0, 0);
  if(r0 != -1){
    printf(1, "FAIL: effect with key 0 returned %d, expected -1\n", r0);
    exit();
  }
  printf(1, "   PASS: key 0 was rejected with -1\n");

  // 2. Test create
  printf(1, "2. Testing file creation with key 1001...\n");
  unlink("test_once.txt");
  int r1 = effect(1001, EFFECT_OP_CREATE, "test_once.txt", 0, 0);
  if(r1 < 0){
    printf(1, "FAIL: effect CREATE returned %d\n", r1);
    exit();
  }
  check_file_size("test_once.txt", 0);
  printf(1, "   PASS: file created, size is 0\n");

  // 3. Test append (first time)
  printf(1, "3. Testing append with key 1002 (first call)...\n");
  int r2 = effect(1002, EFFECT_OP_APPEND, "test_once.txt", "FIRST_APPEND\n", 13);
  if(r2 != 13){
    printf(1, "FAIL: first append returned %d, expected 13\n", r2);
    exit();
  }
  check_file_size("test_once.txt", 13);
  printf(1, "   PASS: first append wrote 13 bytes, file size is 13\n");

  // 4. Test append with SAME key (second call -> MUST be idempotent!)
  printf(1, "4. Testing append with key 1002 (second call, replay)...\n");
  int r3 = effect(1002, EFFECT_OP_APPEND, "test_once.txt", "FIRST_APPEND\n", 13);
  if(r3 != 13){
    printf(1, "FAIL: replay append returned %d, expected cached 13\n", r3);
    exit();
  }
  check_file_size("test_once.txt", 13);
  printf(1, "   PASS: replay returned cached 13, file size is STILL 13 (no double write!)\n");

  // 5. Test append with DIFFERENT key (third call -> should grow file)
  printf(1, "5. Testing append with new key 1003...\n");
  int r4 = effect(1003, EFFECT_OP_APPEND, "test_once.txt", "SECOND_APPEND\n", 14);
  if(r4 != 14){
    printf(1, "FAIL: new key append returned %d, expected 14\n", r4);
    exit();
  }
  check_file_size("test_once.txt", 27);
  printf(1, "   PASS: new key append wrote 14 bytes, file size is now 27 (13 + 14)\n");

  printf(1, "=== ALL effecttest tests PASSED ===\n");
  exit();
}
