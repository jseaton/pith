# pith
A silly virtualisation project

This project exists to answer a stupid question to myself: can I run a single Linux process under KVM, and basically just proxy all of the syscalls out to Linux?
Not to say this is a good idea, for both security and performance reasons. But it might be pleasingly simple. We'll see.

Current state: falls over on syscall instruction.
