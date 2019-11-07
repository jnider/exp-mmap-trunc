# exp-mmap-trunc
Protect the length of a memory mapped file using URCU (userspace RCU library)

Truncating a memory mapped file while other threads are reading from it runs the risk of reading beyond the end of the file.
That would cause a SIGBUS error. Even if using mremap() to change the range of the mapped file, it is possible that a reader
thread still holds and uses the old length of the file. This behaviour is demonstrated in ''exp1''. The solution is to
synchronize between the readers and truncation action. The second experiment ''exp2'' demonstrates how to do this using URCU,
the userspace RCU library.
