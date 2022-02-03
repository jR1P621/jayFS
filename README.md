# jayFS

For Operating Systems course

The attached source code is an implementation of an indexed file system.  An indexed system was chosen for its well organized structure and scalability.

The attached Design.pdf lays out the basic data structures of the file system.  Ideally, I was attempting to implement a file system with no soft name length limit, file count limit, or file size limit by utilizing various implementations of linked lists.  This added some complexity to alorithms as information spilled from one block into another (e.g., Reading a file's name that starts in one block but end in another).  The majority of time was spent designing these algorithms.

Much of the fuctionality was designed to be modular.  For example, rm removes a file's data, then removes it's name, then removes it's node.  The rename function uses the same logic: it creates a new node and adds its name while removing the old name and node.  But instead of removing data or adding new data, the file's data and iNode are preserved and linked to the newly created node.

So far I've tested the file system using the recommended shell commands.  I've tested read and write capabilities for large files by moving a several MB PDF to the file system and successfully reading it from the file system.

If I were to optimize my code, I would try to implement a few more recurrsive algorithms instead of looping algorithms.  I would also look for ways to optimize runtime performance.  I know of a couple algorithms that run at O(n) that could be optimized to either run at a much faster O(n) or even run at O(1).

An extra set of eyes would be very helpful.
