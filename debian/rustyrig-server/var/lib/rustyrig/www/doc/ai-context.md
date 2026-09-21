# AI context / project memory

This file is intended to capture stable project decisions that would
otherwise have to be rediscovered during every coding session.

## Repository relationships

- `rustyrig-fw` is the main C/server/protocol repository.
- `rustyrig-www` is the separate browser WebUI repository.
- The WebUI is a real client implementation, not simply static HTML.

## Important design decisions

### Protocol

`librrprotocol` is the primary place to understand RustyRig protocol
semantics. Do not infer protocol behavior solely from a frontend.

### Client parity

The C native client and JavaScript WebUI intentionally implement the
same underlying client behavior in different languages/frameworks.

When changing shared client behavior, inspect both implementations.

### C

- 3-space indentation.
- Preserve support for GTK and non-GTK builds.
- Avoid unnecessary dependencies.
- Be mindful of eventual small/microcontroller targets for shared code.
- Prefer integer arithmetic where practical.
- Preserve established logging/error-handling conventions.

### Time

Use the project's established time handling. `time_t` is appropriate
when only seconds are needed; higher-resolution elapsed-time work should
use the established `struct timespec` approach rather than inventing a
new timing abstraction.

### Existing code first

Before adding a helper, abstraction, or global, search for an existing
one. This project has accumulated substantial shared infrastructure.

## Things to add here over time

When a project decision would be expensive for an AI to rediscover, add
it here.

Good entries explain:
- what the rule is
- why it exists
- which files/components it affects
- what an agent should inspect before changing it
