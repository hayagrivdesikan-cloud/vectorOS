
import pexpect
import sys
class XV6Bridge:
    def __init__(self):
        self.proc = None

    def start(self):
        self.proc = pexpect.spawn(
            "make", ["qemu-nox"],
            encoding="utf-8",
            timeout=60
        )
        self.proc.logfile_read = sys.stdout
        self.proc.expect(r"\$ ", timeout=120)
        self.proc.logfile_read = None

    def execute(self, command):
        if command not in ["ls", "cat README"]:
            raise ValueError("Command not allowed")

        self.proc.sendline(command)
        self.proc.expect(r"\$ ", timeout=15)
        return self.proc.before.strip()

    def stop(self):
        if self.proc and self.proc.isalive():
            self.proc.sendcontrol("a")
            self.proc.send("x")
            self.proc.close(force=True)

bridge = XV6Bridge()

try:
    bridge.start()
    print("Connected to xv6!")
    print("LISTING:")
    print(bridge.execute("ls"))
    print("README:")
    print(bridge.execute("cat README"))
finally:
    bridge.stop()
