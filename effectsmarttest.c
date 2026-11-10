// effectsmarttest.c — User test for sys_effect_smart replay-within-window protection.

#include "types.h"
#include "stat.h"
#include "user.h"
#include "effect.h"

static uint
get_file_size(const char *path)
{
  struct stat st;
  int fd = open(path, 0);
  if(fd < 0)
    return 0;
  if(fstat(fd, &st) < 0){
    close(fd);
    return 0;
  }
  close(fd);
  return st.size;
}

int
main(int argc, char *argv[])
{
  printf(1, "=== Running effectsmarttest ===\n");
  unlink("smart_out.txt");

  // 1. Within-window duplicate suppression (same process, back-to-back)
  printf(1, "1. Testing within-window duplicate suppression (same process)...\n");
  int r1 = effect_smart(EFFECT_OP_APPEND, "smart_out.txt", "PAYLOAD_A\n", 10);
  if(r1 != 10){
    printf(1, "FAIL: first effect_smart returned %d, expected 10\n", r1);
    exit();
  }
  uint sz1 = get_file_size("smart_out.txt");
  if(sz1 != 10){
    printf(1, "FAIL: file size after first call is %d, expected 10\n", sz1);
    exit();
  }

  // Second call: identical op, path, payload, same process, same window
  int r2 = effect_smart(EFFECT_OP_APPEND, "smart_out.txt", "PAYLOAD_A\n", 10);
  if(r2 != 10){
    printf(1, "FAIL: replay effect_smart returned %d, expected cached 10\n", r2);
    exit();
  }
  uint sz2 = get_file_size("smart_out.txt");
  if(sz2 != 10){
    printf(1, "FAIL: file size after duplicate is %d, expected 10 (suppression failed!)\n", sz2);
    exit();
  }
  printf(1, "   PASS: duplicate call within same window suppressed (file size remains 10)\n");

  // 2. Documented Non-Guarantee #1: Different process (different PID -> new key)
  printf(1, "2. Testing non-guarantee across different processes (fork)...\n");
  int pid = fork();
  if(pid < 0){
    printf(1, "FAIL: fork failed\n");
    exit();
  }
  if(pid == 0){
    // Child process: same payload, but different PID!
    int rc = effect_smart(EFFECT_OP_APPEND, "smart_out.txt", "PAYLOAD_A\n", 10);
    if(rc != 10){
      printf(1, "FAIL: child effect_smart returned %d\n", rc);
      exit();
    }
    exit();
  }
  wait();
  uint sz3 = get_file_size("smart_out.txt");
  if(sz3 != 20){
    printf(1, "FAIL: file size after child is %d, expected 20 (10 + 10)\n", sz3);
    exit();
  }
  printf(1, "   PASS: child process with different PID treated as new effect (size = 20)\n");

  // 3. Documented Non-Guarantee #2: Tick bucket rollover (new window -> new key)
  printf(1, "3. Testing non-guarantee across window rollover (sleeping %d ticks)...\n", WINDOW_TICKS + 10);
  sleep(WINDOW_TICKS + 10);

  int r3 = effect_smart(EFFECT_OP_APPEND, "smart_out.txt", "PAYLOAD_A\n", 10);
  if(r3 != 10){
    printf(1, "FAIL: post-rollover effect_smart returned %d\n", r3);
    exit();
  }
  uint sz4 = get_file_size("smart_out.txt");
  if(sz4 != 30){
    printf(1, "FAIL: file size after rollover is %d, expected 30 (20 + 10)\n", sz4);
    exit();
  }
  printf(1, "   PASS: call after tick bucket rollover treated as new effect (size = 30)\n");

  printf(1, "=== ALL effectsmarttest tests PASSED ===\n");
  exit();
}
