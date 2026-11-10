
TOOLS = {
    "list_files": {"required": []},
    "read_file": {"required": ["path"]},
    "summarize": {"required": ["text"]},
}

def validate_action(action):
    if not isinstance(action, dict):
        raise ValueError("Invalid action")

    name = action.get("tool")
    args = action.get("args")

    if name not in TOOLS:
        raise ValueError("Unknown tool")

    if not isinstance(args, dict):
        raise ValueError("Invalid arguments")

    required = TOOLS[name]["required"]

    if set(args) != set(required):
        raise ValueError("Missing or unexpected arguments")

    if any(not isinstance(v, str) for v in args.values()):
        raise TypeError("Arguments must be strings")

    if name == "read_file" and args["path"] != "README":
        raise ValueError("Path not allowed")


class Tools:
    def execute(self, action):
        validate_action(action)

        name = action["tool"]
        args = action["args"]

        if name == "list_files":
            return "ls"

        if name == "read_file":
            return "cat README"

        if name == "summarize":
            return args["text"][:100]


class Agent:
    def __init__(self, tools):
        self.tools = tools

    def run(self, request):
        if request.startswith("list"):
            action = {"tool": "list_files", "args": {}}

        elif request.startswith("read "):
            path = request[len("read "):].strip()
            action = {
                "tool": "read_file",
                "args": {"path": path}
            }

        else:
            return "Unsupported request"

        validate_action(action)
        return self.tools.execute(action)


agent = Agent(Tools())

print(agent.run("list"))
print(agent.run("read README"))
print(agent.run("delete README"))
