/*

  MyFS: a tiny file-system written for educational purposes

  MyFS is

  Copyright 2018-19 by

  University of Alaska Anchorage, College of Engineering.

  Contributors: Christoph Lauter
                and Jon Rippe
  and based on

  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

gcc -g -O0 -Wall myfs.c implementation.c `pkg-config fuse --cflags --libs` -o
myfs

  The filesystem can be mounted while it is running inside gdb (for
  debugging) purposes as follows (adapt to your setup):

  gdb --args ./myfs --size=50000 --backupfile=test.myfs fuse-mnt/ -f
  gdb --args ./myfs --size=50000 fuse-mnt/ -f
  gdb --args ./myfs fuse-mnt/ -f
  gdb --args ./myfs --backupfile=test.myfs fuse-mnt/ -f

  It can then be unmounted (in another terminal) with

  fusermount -u fuse-mnt

  TESTING CMDS
  __myfs_getattr_implem: ls -la in the mounted directory => several calls
  __myfs_readdir_implem: ls -la in the mounted directory => one call, readdir
  reads the directory, so lists the filenames and subdirectories
  __myfs_mknod_implem: touch file1 in a directory in the filesystem
  __myfs_unlink_implem: rm file2 where file2 is a file in the mounted directory
  (or subdirectory) that does exist (once it works, try the function
  non-existing files as well)
  __myfs_rmdir_implem: rmdir directory where directory is an empty subdirectory
  of the mounted filesystem Second test: leave a file in the directory, so that
  it is not empty, __myfs_rmdir_implem needs to detect that and return a failure
  __myfs_mkdir_implem: mkdir directory in a directory or subdirectory of the
  mounted filessystem, where directory is a non-existing directory, which will
  created. Second test: try to create a directory that already exists or try to
  create a directory when afile of the same name exists
  __myfs_truncate_implem: truncate -s 1000 file17 where file17 is a file that
  a) Does not exist (FUSE might call mknod before)
  b) That exists and is shorter than the new length of 1000 bytes
  c) That exists and is longer than the new length of 1000 bytes
  d) That exists and has precisely 1000 bytes in it already, in which case
  nothing cahnges and the call is successful
  If the file grows larger, we need to append zeros
  memset(ptr_to_where_file_data_is, 0, length)
  __myfs_open_implem: echo "Hello" > test
  __myfs_open_implem: cat test where test is an existing regular file with
  content Second test: try it with a file that does not exist The purpose of
  open in FUSE is just to check if the file exists or not (there is nothing
  "opened" / there is no state)
  __myfs_read_implem: cat text where text is a textfile that exists
  __myfs_read_implem: mplayer video.mp4 where video.mp4 is a movie in the
  filesystem Try it with files > 4kB and <4KBytes In one case you return less
  than 4kB, in the other case, you get another read at the next offset
  __myfs_write_implem: echo "Hello" > file, where file is an existing or new
  file
  __myfs_write_implem: echo "Hello" >> file, where file exists (it's going to
  write at an offset to append to the file)
  __myfs_write_implem: cp ~/movie . where . is a directory in the mounted
  filesystem, cp will write again and again to the file that gets copied
  __myfs_utimens_implem: touch --date="1980-04-24 11:30:00.0 +0100" part1.mkv
  where part1.mkv is an existing or new file the check with ls -la : The file
  whose date got changed needs to be the right date
  __myfs_statfs_implem: df
  __myfs_rename_implem: several cases
  a) rename a file
  mv file new_file
  b) rename a directory
  mv dir new_dir
  c) move a file into another directory, which must exist
  c-1) But the file of that name does not exist in the other directory
  mv file ./dir/
  c-2) But the file exists in the other directory, in which case the existing
  file needs to go away first
  mv file ./dir/ but dir contains file
  d) move a directory into another directory, which does exist
  d-1) The subdirectory exists
  d-2) The subdirectory does not exist





*/

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* The filesystem you implement must support all the 13 operations
   stubbed out below. There need not be support for access rights,
   links, symbolic links. There needs to be support for access and
   modification times and information for statfs.

   The filesystem must run in memory, using the memory of size
   fssize pointed to by fsptr. The memory comes from mmap and
   is backed with a file if a backup-file is indicated. When
   the filesystem is unmounted, the memory is written back to
   that backup-file. When the filesystem is mounted again from
   the backup-file, the same memory appears at the newly mapped
   in virtual address. The filesystem datastructures hence must not
   store any pointer directly to the memory pointed to by fsptr; it
   must rather store offsets from the beginning of the memory region.

   When a filesystem is mounted for the first time, the whole memory
   region of size fssize pointed to by fsptr reads as zero-bytes. When
   a backup-file is used and the filesystem is mounted again, certain
   parts of the memory, which have previously been written, may read
   as non-zero bytes. The size of the memory region is at least 2048
   bytes.

   CAUTION:

   * You MUST NOT use any global variables in your program for reasons
   due to the way FUSE is designed.

   You can find ways to store a structure containing all "global" data
   at the start of the memory region representing the filesystem.

   * You MUST NOT store (the value of) pointers into the memory region
   that represents the filesystem. Pointers are virtual memory
   addresses and these addresses are ephemeral. Everything will seem
   okay UNTIL you remount the filesystem again.

   You may store offsets/indices (of type size_t) into the
   filesystem. These offsets/indices are like pointers: instead of
   storing the pointer, you store how far it is away from the start of
   the memory region. You may want to define a type for your offsets
   and to write two functions that can convert from pointers to
   offsets and vice versa.

   * You may use any function out of libc for your filesystem,
   including (but not limited to) malloc, calloc, free, strdup,
   strlen, strncpy, strchr, strrchr, memset, memcpy. However, your
   filesystem MUST NOT depend on memory outside of the filesystem
   memory region. Only this part of the virtual memory address space
   gets saved into the backup-file. As a matter of course, your FUSE
   process, which implements the filesystem, MUST NOT leak memory: be
   careful in particular not to leak tiny amounts of memory that
   accumulate over time. In a working setup, a FUSE process is
   supposed to run for a long time!

   It is possible to check for memory leaks by running the FUSE
   process inside valgrind:

   valgrind --leak-check=full ./myfs --backupfile=test.myfs ~/fuse-mnt/ -f

   However, the analysis of the leak indications displayed by valgrind
   is difficult as libfuse contains some small memory leaks (which do
   not accumulate over time). We cannot (easily) fix these memory
   leaks inside libfuse.

   * Avoid putting debug messages into the code. You may use fprintf
   for debugging purposes but they should all go away in the final
   version of the code. Using gdb is more professional, though.

   * You MUST NOT fail with exit(1) in case of an error. All the
   functions you have to implement have ways to indicated failure
   cases. Use these, mapping your internal errors intelligently onto
   the POSIX error conditions.

   * And of course: your code MUST NOT SEGFAULT!

   It is reasonable to proceed in the following order:

   (1)   Design and implement a mechanism that initializes a filesystem
         whenever the memory space is fresh. That mechanism can be
         implemented in the form of a filesystem handle into which the
         filesystem raw memory pointer and sizes are translated.
         Check that the filesystem does not get reinitialized at mount
         time if you initialized it once and unmounted it but that all
         pieces of information (in the handle) get read back correctly
         from the backup-file.

   (2)   Design and implement functions to find and allocate free memory
         regions inside the filesystem memory space. There need to be
         functions to free these regions again, too. Any "global" variable
         goes into the handle structure the mechanism designed at step (1)
         provides.

   (3)   Carefully design a data structure able to represent all the
         pieces of information that are needed for files and
         (sub-)directories.  You need to store the location of the
         root directory in a "global" variable that, again, goes into the
         handle designed at step (1).

   (4)   Write __myfs_getattr_implem and debug it thoroughly, as best as
         you can with a filesystem that is reduced to one
         function. Writing this function will make you write helper
         functions to traverse paths, following the appropriate
         subdirectories inside the file system. Strive for modularity for
         these filesystem traversal functions.

   (5)   Design and implement __myfs_readdir_implem. You cannot test it
         besides by listing your root directory with ls -la and looking
         at the date of last access/modification of the directory (.).
         Be sure to understand the signature of that function and use
         caution not to provoke segfaults nor to leak memory.

   (6)   Design and implement __myfs_mknod_implem. You can now touch files
         with

         touch foo

         and check that they start to exist (with the appropriate
         access/modification times) with ls -la.

   (7)   Design and implement __myfs_mkdir_implem. Test as above.

   (8)   Design and implement __myfs_truncate_implem. You can now
         create files filled with zeros:

         truncate -s 1024 foo

   (9)   Design and implement __myfs_statfs_implem. Test by running
         df before and after the truncation of a file to various lengths.
         The free "disk" space must change accordingly.

   (10)  Design, implement and test __myfs_utimens_implem. You can now
         touch files at different dates (in the past, in the future).

   (11)  Design and implement __myfs_open_implem. The function can
         only be tested once __myfs_read_implem and __myfs_write_implem are
         implemented.

   (12)  Design, implement and test __myfs_read_implem and
         __myfs_write_implem. You can now write to files and read the data
         back:

         echo "Hello world" > foo
         echo "Hallo ihr da" >> foo
         cat foo

         Be sure to test the case when you unmount and remount the
         filesystem: the files must still be there, contain the same
         information and have the same access and/or modification
         times.

   (13)  Design, implement and test __myfs_unlink_implem. You can now
         remove files.

   (14)  Design, implement and test __myfs_unlink_implem. You can now
         remove directories.

   (15)  Design, implement and test __myfs_rename_implem. This function
         is extremely complicated to implement. Be sure to cover all
         cases that are documented in man 2 rename. The case when the
         new path exists already is really hard to implement. Be sure to
         never leave the filessystem in a bad state! Test thoroughly
         using mv on (filled and empty) directories and files onto
         inexistant and already existing directories and files.

   (16)  Design, implement and test any function that your instructor
         might have left out from this list. There are 13 functions
         __myfs_XXX_implem you have to write.

   (17)  Go over all functions again, testing them one-by-one, trying
         to exercise all special conditions (error conditions): set
         breakpoints in gdb and use a sequence of bash commands inside
         your mounted filesystem to trigger these special cases. Be
         sure to cover all funny cases that arise when the filesystem
         is full but files are supposed to get written to or truncated
         to longer length. There must not be any segfault; the user
         space program using your filesystem just has to report an
         error. Also be sure to unmount and remount your filesystem,
         in order to be sure that it contents do not change by
         unmounting and remounting. Try to mount two of your
         filesystems at different places and copy and move (rename!)
         (heavy) files (your favorite movie or song, an image of a cat
         etc.) from one mount-point to the other. None of the two FUSE
         processes must provoke errors. Find ways to test the case
         when files have holes as the process that wrote them seeked
         beyond the end of the file several times. Your filesystem must
         support these operations at least by making the holes explicit
         zeros (use dd to test this aspect).

   (18)  Run some heavy testing: copy your favorite movie into your
         filesystem and try to watch it out of the filesystem.

*/

/* Helper types and functions */
typedef unsigned int block_t;
#define BLOCK_SIZE ((size_t)(4096))
#define ROOT ((block_t)(1))
#define TRUE 1
#define FALSE 0
#define FILE (mode_t)(S_IFREG | 0755)
#define DIR (mode_t)(S_IFDIR | 0755)

struct _fileAttr {
  off_t name;
  block_t iNode;
  off_t size;
  nlink_t nlink;
  struct timespec atim;
  struct timespec mtim;
};
typedef struct _fileAttr _file_attr_t;

struct _fsHeader {
  size_t fsSize;
  size_t blockSize;
  int totalBlocks;
  int freeBlocks;
  block_t firstFree;
  block_t lastFree;
  struct _fileAttr rtAttr;
};
typedef struct _fsHeader _fs_header_t;

struct _dirBlock {
  struct _fileAttr
      files[(BLOCK_SIZE - (2 * sizeof(block_t))) / sizeof(_file_attr_t)];
  int fileCount;
  block_t overflow;
  block_t filenames;
};
typedef struct _dirBlock _dir_block_t;

struct _dirOverflowBlock {
  struct _fileAttr
      files[(BLOCK_SIZE - (sizeof(block_t))) / sizeof(_file_attr_t)];
  block_t overflow;
};
typedef struct _dirOverflowBlock _dir_overflow_block_t;

struct _filenameBlock {
  char names[BLOCK_SIZE - sizeof(block_t)];
  block_t overflow;
};
typedef struct _filenameBlock _filename_block_t;

struct _freeBlock {
  block_t next;
};
typedef struct _freeBlock _free_block_t;

struct _fileBlock {
  block_t data[(BLOCK_SIZE / sizeof(block_t)) - 1];
  block_t overflow;
};
typedef struct _fileBlock _file_block_t;

/* YOUR HELPER FUNCTIONS GO HERE */

/*
  Returns a pointer to the memory address at the beginning of the
  specified block number.
  Returns NULL on failure.
*/
void *ptrTo(_fs_header_t *header, const block_t blockNum) {
  if (blockNum < 0 || blockNum >= header->totalBlocks) {
    return NULL;
  }
  return (void *)((void *)header + (blockNum * BLOCK_SIZE));
}

/*
Returns 0 if path is not valid
Returns 1 if path is valid
*/
int validPath(const char *path) {
  if (strlen(path) == 0) {
    return FALSE;
  }
  if (path[0] != '/') {
    return FALSE;
  }
  if (strpbrk(path, "!@#$\%^&*\\|?") != NULL) {
    return FALSE;
  }
  return TRUE;
}

/*
  If filesystem is not setup, it creates a new file system.
  If filesystem is setup, it does nothing.
  Returns -1 on failure.
  Returns 0 on success.
*/
int createFilesystem(_fs_header_t *header, const size_t fssize) {
  if (header->fsSize != fssize - BLOCK_SIZE || header->rtAttr.name != 0 ||
      header->blockSize != BLOCK_SIZE) {

    // Enforce min fs size of 16KB
    if (fssize < 16384) {
      return -1;
    } else {
      header->blockSize = BLOCK_SIZE;
    }

    // Fill header block
    header->fsSize = fssize - BLOCK_SIZE;
    header->totalBlocks = fssize / BLOCK_SIZE;
    header->freeBlocks = header->totalBlocks - 2;
    header->firstFree = 2;
    header->rtAttr.name = 0;
    header->rtAttr.iNode = 1;
    header->rtAttr.size = BLOCK_SIZE;
    header->rtAttr.nlink = 2;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    header->rtAttr.atim = ts;
    header->rtAttr.mtim = ts;

    // Create root i node
    _dir_block_t *root = (_dir_block_t *)(ptrTo(header, ROOT));
    root->fileCount = 0;
    root->overflow = 0;
    root->filenames = 0;

    // Setup free space linked list
    for (int i = 2; i < header->totalBlocks - 1; i++) {
      ((_free_block_t *)(ptrTo(header, (block_t)i)))->next = (block_t)(i + 1);
    }
    // Last entry
    header->lastFree = (block_t)(header->totalBlocks) - 1;
    ((_free_block_t *)(ptrTo(header, header->lastFree)))->next = (block_t)(0);
  }

  return 0;
}

/*
Adds block to free list.
*/
void freeupBlock(_fs_header_t *header, const block_t blockNum) {
  block_t curr = header->firstFree, prev;

  // Place in linked list
  if (curr == 0) { // no free blocks
    header->firstFree = blockNum;
    header->lastFree = blockNum;
    ((_free_block_t *)(ptrTo(header, blockNum)))->next = 0;
  } else if (curr > blockNum) { // new head
    header->firstFree = blockNum;
    ((_free_block_t *)(ptrTo(header, blockNum)))->next = curr;
  } else if (header->lastFree < blockNum) { // new tail
    ((_free_block_t *)(ptrTo(header, header->lastFree)))->next = blockNum;
    ((_free_block_t *)(ptrTo(header, blockNum)))->next = (block_t)0;
  } else { // in the middle
    while (curr < blockNum) {
      prev = curr;
      curr = ((_free_block_t *)(ptrTo(header, curr)))->next;
    }
    ((_free_block_t *)(ptrTo(header, prev)))->next = blockNum;
    ((_free_block_t *)(ptrTo(header, blockNum)))->next = curr;
  }
  header->freeBlocks++;
  return;
}

/*
  Recursively frees up name blocks.  Does not free starting name block.
  Updates directory size accordingly
*/
void freeupNameBlocksRecur(_fs_header_t *header, _filename_block_t *start,
                           _file_attr_t *pwdAttr) {
  if (start->overflow == 0) {
    return;
  }
  freeupNameBlocksRecur(
      header, (_filename_block_t *)(ptrTo(header, start->overflow)), pwdAttr);
  freeupBlock(header, start->overflow);
  start->overflow = 0;
  pwdAttr->size -= BLOCK_SIZE;

  return;
}

/*
Returns the first free block in free block list.
Updates list head to next free block.
Zeros out block.
Returns block number on success.
Returns 0 on failure.
*/
block_t getFreeBlock(_fs_header_t *header) {
  block_t freeBlock = header->firstFree;
  if (freeBlock == 0) { // No available blocks
    return 0;
  }
  if (header->firstFree == header->lastFree) { // Last free block
    header->lastFree = 0;
  }
  header->firstFree = ((_free_block_t *)(ptrTo(header, freeBlock)))->next;
  memset(ptrTo(header, freeBlock), 0, BLOCK_SIZE);
  header->freeBlocks--;
  return freeBlock;
}

/*
  Recursively reads data into provided buffer starting at specified inode data
  block and offset
*/
void readDataIntoBufferRecurs(_fs_header_t *header, char *buf, size_t size,
                              off_t offset, int blockIndex,
                              _file_block_t *currINode) {
  if (size == 0) {
    return;
  }
  size_t blockArrSize = sizeof(currINode->data) / sizeof(block_t);
  if (blockIndex >= blockArrSize) {
    currINode = (_file_block_t *)(ptrTo(header, currINode->overflow));
    blockIndex = 0;
  }
  size_t nextBlockSize = 0;
  if (offset + size >= BLOCK_SIZE) {
    nextBlockSize = size - (BLOCK_SIZE - offset);
    size = BLOCK_SIZE - offset;
  }
  void *dataBlock = ptrTo(header, currINode->data[blockIndex]);
  memcpy(buf, dataBlock + offset, size);
  readDataIntoBufferRecurs(header, buf + size, nextBlockSize, 0, blockIndex + 1,
                           currINode);
  return;
}

/*
  Recursively writes data from provided buffer starting at specified inode data
  block and offset.
  Returns number of bytes written.
*/
int writeDataIntoFileRecurs(_fs_header_t *header, const char *buf, size_t size,
                            off_t offset, int blockIndex,
                            _file_block_t *currINode) {
  if (size == 0) {
    return 0;
  }
  size_t blockArrSize = sizeof(currINode->data) / sizeof(block_t);

  // Need a new iNode
  if (blockIndex >= blockArrSize) {
    if (currINode->overflow == 0) {
      currINode->overflow = getFreeBlock(header);
      if (currINode->overflow == 0) {
        return 0;
      }
    }
    currINode = (_file_block_t *)(ptrTo(header, currINode->overflow));
    blockIndex = 0;
  }

  size_t nextBlockSize = 0;
  // Can't write entire buffer in this data block, set next size
  if (offset + size >= BLOCK_SIZE) {
    nextBlockSize = size - (BLOCK_SIZE - offset);
    size = BLOCK_SIZE - offset;
  }

  // See if data block exists
  if (currINode->data[blockIndex] == 0) {
    currINode->data[blockIndex] = getFreeBlock(header);
    if (currINode->data[blockIndex] == 0) {
      return 0;
    }
  }
  void *dataBlock = ptrTo(header, currINode->data[blockIndex]);
  memcpy(dataBlock + offset, buf, size);
  return (int)(size + writeDataIntoFileRecurs(header, buf + size, nextBlockSize,
                                              0, blockIndex + 1, currINode));
}

/*
  Searches for a file name and returns the index number of the fileAttr struct.
  Returns -1 if file is not found.
*/
int getFileNumByName(_fs_header_t *header, char *strToFind,
                     const _file_attr_t *pwdAttr) {
  if (pwdAttr->iNode == 0) {
    return -1;
  }
  _dir_block_t *pwd = (_dir_block_t *)(ptrTo(header, pwdAttr->iNode));
  _filename_block_t *nameBlock =
      (_filename_block_t *)(ptrTo(header, pwd->filenames));
  int numCounter = 0, limit = pwd->fileCount, found = FALSE;
  size_t nameArrSize = sizeof(nameBlock->names);
  char *nPtr = strToFind, *mPtr = nameBlock->names;
  while (numCounter < limit && found == FALSE) {
    found = TRUE;
    do {
      if (*nPtr != *mPtr) {
        found = FALSE;
      }
      if (*nPtr != '\0') {
        nPtr++;
      }
      mPtr++;
    } while (mPtr < &(nameBlock->names[nameArrSize]) && *(mPtr - 1) != '\0');
    if (*(mPtr - 1) == '\0' && found == FALSE) {
      numCounter++;
      nPtr = strToFind;
    }
    if (mPtr == &(nameBlock->names[nameArrSize])) {
      nameBlock = (_filename_block_t *)(ptrTo(header, nameBlock->overflow));
      mPtr = nameBlock->names;
    }
  }
  if (found == TRUE) {
    return numCounter;
  }
  return -1;
}

/*
  Returns a pointer to the fileAttr struct of a given index number.
  Returns NULL if invalid.
*/
_file_attr_t *getFileAttrByNum(_fs_header_t *header, int fileNum,
                               const _file_attr_t *pwdAttr) {
  if (pwdAttr->iNode == 0) {
    return NULL;
  }
  _dir_block_t *pwd = (_dir_block_t *)(ptrTo(header, pwdAttr->iNode));
  if (fileNum >= pwd->fileCount) {
    return NULL;
  }
  int limit = sizeof(pwd->files);
  if (fileNum < limit) { // stay in this block
    return &(pwd->files[fileNum]);
  } else { // Run through overflow blocks
    fileNum -= limit;
    _dir_overflow_block_t *overflowBlock =
        (_dir_overflow_block_t *)(ptrTo(header, pwd->overflow));
    limit = sizeof(overflowBlock->files);
    while (fileNum >= limit) {
      fileNum -= limit;
      overflowBlock =
          (_dir_overflow_block_t *)(ptrTo(header, overflowBlock->overflow));
    }
    return &(overflowBlock->files[fileNum]);
  }
}

/*
  Returns a pointer to a cstring allocated in memory containing a file's name
  Returns NULL if invalid.
*/
char *getFileNameToHeap(_fs_header_t *header, const _file_attr_t *thisFile,
                        const _file_attr_t *pwdAttr) {
  if (pwdAttr->iNode == 0) {
    return NULL;
  }
  _dir_block_t *pwd = (_dir_block_t *)(ptrTo(header, pwdAttr->iNode));
  _filename_block_t *nameBlock =
      (_filename_block_t *)(ptrTo(header, pwd->filenames));
  size_t nameArrSize = sizeof(nameBlock->names);
  off_t nameOff = thisFile->name;
  while (nameOff >= nameArrSize) {
    nameBlock = (_filename_block_t *)(ptrTo(header, nameBlock->overflow));
    nameOff -= nameArrSize;
  }

  char nameBuf[4096], *nPtr = &(nameBlock->names[nameOff]), *bPtr = nameBuf;
  void *filenamePtr = NULL;
  size_t allocSize = 0;
  do {
    // If at the end of name block, cont at next name block
    if (nPtr == ((nameBlock->names) + nameArrSize)) {
      nameBlock = (_filename_block_t *)(ptrTo(header, nameBlock->overflow));
      nPtr = nameBlock->names;
    }
    // At the end of the buffer, flush to heap
    if (bPtr - nameBuf == sizeof(nameBuf)) {
      filenamePtr = realloc(filenamePtr, allocSize + sizeof(nameBuf));
      if (filenamePtr == NULL) {
        return NULL;
      }
      memcpy(filenamePtr + allocSize, (const void *)(nameBuf), sizeof(nameBuf));
      allocSize += sizeof(nameBuf);
      bPtr = nameBuf;
    }
    *bPtr = *nPtr;
    bPtr++;
    nPtr++;
  } while (*(nPtr - 1) != '\0');
  // At the end of file name, flush to heap
  filenamePtr = realloc(filenamePtr, allocSize + strlen(nameBuf) + 1);
  if (filenamePtr == NULL) {
    return NULL;
  }
  memcpy(filenamePtr + allocSize, (const void *)(nameBuf), strlen(nameBuf) + 1);
  return (char *)filenamePtr;
}

/*
  Follows path to find desired file.
  Algorithm uses strtok to parse the path and then uses a journal to handle .
  and .. to prevent the need for backwards navigation.
  Returns a ptr to the fileAttr struct of a file referenced by an absolute path.
  Returns NULL on error and sets erroptr.
*/
_file_attr_t *getFileAttr(_fs_header_t *header, int *errnoptr,
                          const char *path) {
  // Get max path depth
  char pathBuf[strlen(path)];
  strncpy(pathBuf, path, strlen(path) + 1);
  char *currPathFile = strtok(pathBuf, "/");
  int pathDepth = 1;
  while (currPathFile != NULL) {
    pathDepth++;
    currPathFile = strtok(NULL, "/");
  }

  // If at root
  if (pathDepth == 1) {
    return &(header->rtAttr);
  }

  // Get optimized file block list
  _file_attr_t *pathFileAttr[pathDepth];
  int pathFileNums[pathDepth - 1], blockCount = 1, currentFileNum;
  pathFileAttr[0] = &(header->rtAttr);
  strncpy(pathBuf, path, strlen(path) + 1);
  currPathFile = strtok(pathBuf, "/");
  while (currPathFile != NULL) {
    // Shouldn't be here if we've encountered a non-dir
    if ((int)(pathFileAttr[blockCount - 1]->nlink) == 1) {
      *errnoptr = ENOTDIR;
      return NULL;
    }
    if (strcmp(currPathFile, ".") == 0) {
      // Do Nothing on "."
    } else if (strcmp(currPathFile, "..") == 0) {
      // Go back unless @ root
      if (blockCount > 1) {
        blockCount--;
      }
    } else {
      currentFileNum =
          getFileNumByName(header, currPathFile, pathFileAttr[blockCount - 1]);
      // File/Dir not found
      if (currentFileNum < 0) {
        *errnoptr = ENOENT;
        return NULL;
      }
      pathFileAttr[blockCount] = getFileAttrByNum(header, currentFileNum,
                                                  pathFileAttr[blockCount - 1]);
      pathFileNums[blockCount - 1] = currentFileNum;
      blockCount++;
    }
    currPathFile = strtok(NULL, "/");
  }

  // Return value based on path blocks
  if (blockCount == 1) { // ROOT
    return ((_file_attr_t *)(&(header->rtAttr)));
  }
  return getFileAttrByNum(header, pathFileNums[blockCount - 2],
                          pathFileAttr[blockCount - 2]);
}

/*
  Appends the string to the end of the dir filename listing.
  Sets fileAttr->name offset for the last file in the dir.
  Allocates new filename block overflow if needed.
  Returns offset in name array to new name.
  Returns (off_t)BLOCK_SIZE on failure.
*/
int addNameToFileAttr(_fs_header_t *header, char *strName,
                      _file_attr_t *fileAttr, _file_attr_t *pwdAttr) {
  _dir_block_t *pwd = (_dir_block_t *)(ptrTo(header, pwdAttr->iNode));
  _filename_block_t *filenames, *filenamesORIG = NULL;
  off_t newFileNameOff, newFileArrOffset;
  size_t nameArrSize;

  // Find starting point
  if (pwd->fileCount == 0) { // Adding first file to dir
    pwd->filenames = getFreeBlock(header);
    if (pwd->filenames == 0) {
      return (off_t)BLOCK_SIZE;
    }
    pwdAttr->size += BLOCK_SIZE;
    newFileArrOffset = 0;
    newFileNameOff = 0;
    filenames = (_filename_block_t *)(ptrTo(header, pwd->filenames));
    nameArrSize = sizeof(filenames->names);
  } else { // Get offset of last filename and follow until '\0'
    filenames = (_filename_block_t *)(ptrTo(header, pwd->filenames));
    off_t prevNameOff =
        getFileAttrByNum(header, pwd->fileCount - 1, pwdAttr)->name;
    nameArrSize = sizeof(filenames->names);
    newFileNameOff = prevNameOff;

    // Navigate to current filename block
    while (prevNameOff >= nameArrSize) {
      prevNameOff -= nameArrSize;
      filenames = (_filename_block_t *)(ptrTo(header, filenames->overflow));
    }
    filenamesORIG = filenames;
    newFileArrOffset = prevNameOff;

    // Find the end of the previous file's name
    do {
      if (newFileArrOffset >= nameArrSize) {
        newFileArrOffset = 0;
        filenames = (_filename_block_t *)(ptrTo(header, filenames->overflow));
      }
      newFileArrOffset++;
      newFileNameOff++;
    } while (newFileArrOffset <= nameArrSize &&
             filenames->names[newFileArrOffset - 1] != '\0');

    // Are we starting the new filename right at a new block?
    if (newFileArrOffset >= nameArrSize) {
      newFileArrOffset = 0;
      filenames->overflow = getFreeBlock(header);
      if (filenames->overflow == 0) {
        return (off_t)BLOCK_SIZE;
      }
      pwdAttr->size += BLOCK_SIZE;
    }
  }

  // Write new name
  char *nPtr = &(filenames->names[newFileArrOffset]), *sPtr = strName;
  do {
    // End of name block, ask for overflow
    if (nPtr >= &(filenames->names[nameArrSize])) {
      filenames->overflow = getFreeBlock(header);
      if (filenames->overflow == 0) { // failed to create nameblock
        if (filenamesORIG == NULL) {
          freeupNameBlocksRecur(
              header, (_filename_block_t *)(ptrTo(header, pwd->filenames)),
              pwdAttr);
          freeupBlock(header, pwd->filenames);
          fileAttr->size -= BLOCK_SIZE;
          pwd->filenames = 0;
        } else {
          freeupNameBlocksRecur(header, filenamesORIG, pwdAttr);
        }
        return (off_t)BLOCK_SIZE;
      }
      pwdAttr->size += BLOCK_SIZE;
      filenames = (_filename_block_t *)(ptrTo(header, filenames->overflow));
      nPtr = (char *)filenames;
    }
    // write to name block
    *nPtr = *sPtr;
    nPtr++;
    sPtr++;
  } while (*sPtr != '\0');

  // Return total name offset
  return (off_t)newFileNameOff;
}

/*
  Adds a new fileAttr to dir->files[].  Allocates dir overflow if needed.
  Initializes values in new fileAttr and updates counters.
  Returns files[] index # of new file on success.
  Returns -1 on failure.
*/
int addFileNode(_fs_header_t *header, char *strName, const mode_t type,
                _file_attr_t *pwdAttr) {
  if (pwdAttr->iNode == 0) {
    pwdAttr->iNode = getFreeBlock(header);
    if (pwdAttr->iNode == 0) {
      return -1;
    }
    pwdAttr->size += BLOCK_SIZE;
  }
  _dir_block_t *pwd = (_dir_block_t *)(ptrTo(header, pwdAttr->iNode));
  _dir_overflow_block_t *pwdOverflow = NULL;
  _file_attr_t *pwdFiles = pwd->files;
  size_t fileArrSize = sizeof(pwd->files) / sizeof(_file_attr_t);
  int newFileNum, allocationCheck = FALSE;
  if (pwd->fileCount == fileArrSize) {
    pwd->overflow = getFreeBlock(header);
    allocationCheck = TRUE;
    if (pwd->overflow == 0) {
      return -1;
    }
    pwdAttr->size += BLOCK_SIZE;
    pwdOverflow = (_dir_overflow_block_t *)(ptrTo(header, pwd->overflow));
    pwdFiles = pwdOverflow->files;
    newFileNum = 0;
  } else if (pwd->fileCount > fileArrSize) {
    pwdOverflow = (_dir_overflow_block_t *)(ptrTo(header, pwd->overflow));
    fileArrSize += sizeof(pwdOverflow->files) / sizeof(_file_attr_t);
    while (pwd->fileCount > fileArrSize) {
      pwdOverflow =
          (_dir_overflow_block_t *)(ptrTo(header, pwdOverflow->overflow));
      fileArrSize += sizeof(pwdOverflow->files) / sizeof(_file_attr_t);
    }
    if (pwd->fileCount == fileArrSize) {
      pwdOverflow->overflow = getFreeBlock(header);
      allocationCheck = TRUE;
      if (pwdOverflow->overflow == 0) {
        return -1;
      }
      pwdAttr->size += BLOCK_SIZE;
      pwdOverflow =
          (_dir_overflow_block_t *)(ptrTo(header, pwdOverflow->overflow));
      newFileNum = 0;
    } else {
      newFileNum =
          (pwd->fileCount - sizeof(pwd->files)) % sizeof(pwdOverflow->files);
    }
    pwdFiles = pwdOverflow->files;
  } else {
    newFileNum = pwd->fileCount;
  }
  pwdFiles[newFileNum].name = 0;
  pwdFiles[newFileNum].iNode = 0;
  pwdFiles[newFileNum].size = 0;
  if (type == DIR) {
    pwdFiles[newFileNum].nlink = 2;
  } else {
    pwdFiles[newFileNum].nlink = 1;
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  pwdFiles[newFileNum].atim = ts;
  pwdFiles[newFileNum].mtim = ts;

  off_t nameOffset =
      addNameToFileAttr(header, strName, &pwdFiles[newFileNum], pwdAttr);
  if (nameOffset == BLOCK_SIZE) {
    if (allocationCheck == TRUE) {
      fileArrSize = sizeof(pwd->files) / sizeof(_file_attr_t);
      if (pwd->fileCount == fileArrSize) {
        freeupBlock(header, pwd->overflow);
        pwdAttr->size -= BLOCK_SIZE;
        pwd->overflow = 0;
      } else {
        pwdOverflow = (_dir_overflow_block_t *)(ptrTo(header, pwd->overflow));
        fileArrSize += sizeof(pwdOverflow->files) / sizeof(_file_attr_t);
        while (pwd->fileCount > fileArrSize) {
          pwdOverflow =
              (_dir_overflow_block_t *)(ptrTo(header, pwdOverflow->overflow));
          fileArrSize += sizeof(pwdOverflow->files) / sizeof(_file_attr_t);
        }
        freeupBlock(header, pwdOverflow->overflow);
        pwdAttr->size -= BLOCK_SIZE;
        pwdOverflow->overflow = 0;
      }
    }
    return -1;
  }

  pwdFiles[newFileNum].name = nameOffset;

  if (type == DIR) {
    pwdAttr->nlink++;
  }
  pwd->fileCount++;
  return pwd->fileCount - 1;
}

/*
  Creates a node in pwd for a new file and appends name to name block.
  Allocates new dirOverflow/filenameBlock as necessary.
  No i-node block is allocated for new file/dir
  On failure, sets errnoptr and returns -1
*/
int addFile(_fs_header_t *header, int *errnoptr, const mode_t type,
            const char *path) {
  // Fix trailing '/'
  char pathBuf[strlen(path)];
  strcpy(pathBuf, path);
  while (pathBuf[strlen(pathBuf) - 1] == '/') {
    pathBuf[strlen(pathBuf) - 1] = '\0';
  }
  // Split path into directory & filename
  char *newFileName = strrchr(pathBuf, '/') + 1;
  *(newFileName - 1) = '\0';

  // See if valid file name
  if (strcmp(newFileName, ".") == 0 || strcmp(newFileName, "..") == 0) {
    *errnoptr = EINVAL;
    return -1;
  }

  // get directory
  _file_attr_t *foundDir = getFileAttr(header, errnoptr, pathBuf);
  if (foundDir == NULL) {
    *errnoptr = ENOENT;
    return -1;
  }

  // See if file exists
  if (getFileNumByName(header, newFileName, foundDir) != -1) {
    *errnoptr = EEXIST;
    return -1;
  }

  // Add node to dir
  int newFileNum = addFileNode(header, newFileName, type, foundDir);
  if (newFileNum == -1) {
    *errnoptr = EINVAL;
    return -1;
  }

  return 0;
}

/*
  Searches through a file's inode blocks until reaching the block containing the
  specified data block.
  Returns a pointer to an inode block on success.

*/
_file_block_t *getFileINodeByBlockIndex(_fs_header_t *header, int blockIndex,
                                        _file_attr_t *thisFile) {
  if (thisFile->iNode == 0) {
    return NULL;
  }
  _file_block_t *thisINode = (_file_block_t *)(ptrTo(header, thisFile->iNode));
  size_t blockArrSize = sizeof(thisINode->data) / sizeof(block_t);
  while (blockIndex >= blockArrSize) {
    thisINode = (_file_block_t *)(ptrTo(header, thisINode->overflow));
    blockIndex -= blockArrSize;
  }
  return thisINode;
}

/*
  Deallocates count blocks from foundFile starting at blockIndex.
  Deallocates file inodes if necessary.
*/
void freeupDataBlocksRecurs(_fs_header_t *header, int blockIndex, size_t count,
                            _file_block_t *currINode) {
  if (count == 0) {
    return;
  }
  size_t blockArrSize = sizeof(currINode->data) / sizeof(block_t);
  if (blockIndex == blockArrSize) {
    currINode = (_file_block_t *)(ptrTo(header, currINode->overflow));
    blockIndex = 0;
  }
  freeupDataBlocksRecurs(header, blockIndex + 1, count - 1, currINode);
  freeupBlock(header, currINode->data[blockIndex]);
  currINode->data[blockIndex] = 0;
  if (blockIndex == blockArrSize - 1 && count > 1) {
    freeupBlock(header, currINode->overflow);
    currINode->overflow = 0;
  }
  return;
}

/*
  Allocates empty data blocks to increase foundFile size to offset.
  Offset must be greater than current file size.
  Allocates extra inode blocks as necessary.
  Returns 0 on success.
  Returns -1 on failure.
*/
int increaseFileSize(_fs_header_t *header, off_t offset,
                     _file_attr_t *foundFile) {
  int oldBlockCount = (1 + ((foundFile->size - 1) / BLOCK_SIZE)),
      newBlockCount = (1 + ((offset - 1) / BLOCK_SIZE));
  _file_block_t *currINode =
      getFileINodeByBlockIndex(header, oldBlockCount + 1, foundFile);

  // If filesize is 0 add inode block
  if (foundFile->iNode == 0) {
    foundFile->iNode = getFreeBlock(header);
    if (foundFile->iNode == 0) {
      return -1;
    }
    currINode = (_file_block_t *)(ptrTo(header, foundFile->iNode));
    currINode->data[0] = getFreeBlock(header);
    if (currINode->data[0] == 0) {
      return -1;
    }
  }

  size_t blockArrSize = sizeof(currINode->data) / sizeof(block_t);
  int blocksToWrite = newBlockCount - oldBlockCount,
      startingBlock = oldBlockCount % blockArrSize;

  // allocate data blocks as needed
  if (blocksToWrite > header->freeBlocks) {
    return -1;
  }
  for (int i = 0, blockNum = startingBlock; i < blocksToWrite;
       i++, blockNum++) {
    if (blockNum >= blockArrSize) {
      blockNum = 0;
      currINode->overflow = getFreeBlock(header);
      if (currINode->overflow == 0) {
        freeupDataBlocksRecurs(
            header, startingBlock, (size_t)i,
            getFileINodeByBlockIndex(header, oldBlockCount + 1, foundFile));
        return -1;
      }
      currINode = (_file_block_t *)(ptrTo(header, currINode->overflow));
    }
    currINode->data[blockNum] = getFreeBlock(header);
    if (currINode->data[blockNum] == 0) {
      freeupDataBlocksRecurs(
          header, startingBlock, (size_t)i,
          getFileINodeByBlockIndex(header, oldBlockCount + 1, foundFile));
      return -1;
    }
  }
  return 0;
}

/*
  Deallocates data blocks to accomodate new file size.
  Zeros out tailing data block entries after new file size.
  Offset must be less than current file size.
  Deallocates inode blocks as necessary.
*/
void decreaseFileSize(_fs_header_t *header, off_t offset,
                      _file_attr_t *foundFile) {
  int oldBlockCount = (1 + ((foundFile->size - 1) / BLOCK_SIZE)),
      newBlockCount = (1 + ((offset - 1) / BLOCK_SIZE));
  _file_block_t *currINode =
      getFileINodeByBlockIndex(header, newBlockCount, foundFile);

  size_t blockArrSize = sizeof(currINode->data) / sizeof(block_t);
  int blocksToRemove = oldBlockCount - newBlockCount,
      startingBlock = newBlockCount % blockArrSize;
  freeupDataBlocksRecurs(header, startingBlock, (size_t)blocksToRemove,
                         currINode);
  if (offset == 0) {
    freeupBlock(header, currINode->data[0]);
    currINode->data[0] = 0;
    freeupBlock(header, foundFile->iNode);
    foundFile->iNode = 0;
  }
  return;
}

/*
Copies data from one fileAttr to another.
Pass in VOID as source to zero out dest.
*/
void copyFileAttr(_file_attr_t *dest, _file_attr_t *src) {
  if (src == NULL) {
    _file_attr_t blank = {};
    src = &blank;
  }
  dest->name = src->name;
  dest->size = src->size;
  dest->iNode = src->iNode;
  dest->nlink = src->nlink;
  dest->atim = src->atim;
  dest->mtim = src->mtim;
  return;
}

/*
Finds specified dir overflow block and frees it
Updates dir size accordingly
*/
void freeupDirOverflowBlock(_fs_header_t *header,
                            _dir_overflow_block_t *overflowBlock,
                            _file_attr_t *pwdAttr) {
  _dir_block_t *pwd = ptrTo(header, pwdAttr->iNode);
  if (ptrTo(header, pwd->overflow) == overflowBlock) {
    freeupBlock(header, pwd->overflow);
  } else {
    _dir_overflow_block_t *pwdOverflow = ptrTo(header, pwd->overflow);
    while (ptrTo(header, pwdOverflow->overflow) != overflowBlock) {
      pwdOverflow =
          (_dir_overflow_block_t *)(ptrTo(header, pwdOverflow->overflow));
    }
    freeupBlock(header, pwdOverflow->overflow);
  }
  pwdAttr->size -= BLOCK_SIZE;
  return;
}

/*
  Removes filename from filename block and shifts other filenames down
  Returns count of chars removed.
*/
int removeFileName(_fs_header_t *header, int fileNum, _file_attr_t *pwdAttr) {
  _dir_block_t *pwd = (_dir_block_t *)(ptrTo(header, pwdAttr->iNode));

  // Remove Name & shift names
  // Get offset of filename
  _filename_block_t *currFilenames =
      (_filename_block_t *)(ptrTo(header, pwd->filenames));
  _filename_block_t *nextFilenames = currFilenames;
  off_t nameOff = getFileAttrByNum(header, fileNum, pwdAttr)->name;
  size_t nameArrSize = sizeof(currFilenames->names);
  // Navigate to current filename block
  while (nameOff >= nameArrSize) {
    nameOff -= nameArrSize;
    currFilenames =
        (_filename_block_t *)(ptrTo(header, currFilenames->overflow));
  }

  // Get remove count
  int removedCount = 0;
  char *nPtr = currFilenames->names + nameOff;
  char *mPtr = nPtr;
  do {
    if (nPtr >= nextFilenames->names + nameArrSize) {
      nextFilenames =
          (_filename_block_t *)(ptrTo(header, nextFilenames->overflow));
      nPtr = nextFilenames->names;
    }
    removedCount++;
    nPtr++;
  } while (*(nPtr - 1) != '\0');

  // Shift all following names down to fill gap
  char *nPtrNext = nPtr + 1;
  int endReached = FALSE;
  do {
    if (mPtr >= currFilenames->names + nameArrSize) { // m overflows
      currFilenames =
          (_filename_block_t *)(ptrTo(header, currFilenames->overflow));
      mPtr = currFilenames->names;
    }
    if (nPtr != nPtrNext - 1) { // n overflows
      nPtr = nPtrNext - 1;
    }
    if (nPtrNext >= nextFilenames->names + nameArrSize) { // nNext overflows
      if (nextFilenames->overflow == 0) {
        endReached = TRUE;
      } else {
        nextFilenames =
            (_filename_block_t *)(ptrTo(header, nextFilenames->overflow));
        nPtrNext = nextFilenames->names;
      }
    }
    // Double NULL tells us we're at the end
    if (*nPtr == '\0' && *nPtrNext == '\0') {
      endReached = TRUE;
    }
    *mPtr = *nPtr;
    mPtr++;
    nPtr++;
    nPtrNext++;
  } while (!endReached);

  // Remove trailing junk data
  for (int i = 0;
       i < removedCount && mPtr <= currFilenames->names + nameArrSize;
       i++, mPtr++) {
    // No longer need tail name block
    if (mPtr >= currFilenames->names + nameArrSize) {
      freeupNameBlocksRecur(header, currFilenames, pwdAttr);
    } else {
      *mPtr = '\0';
    }
  }
  return removedCount;
}

/*
  Removes a files name and node from a dir
*/
void removeFileNode(_fs_header_t *header, int fileNum, _file_attr_t *pwdAttr) {
  _dir_block_t *pwd = (_dir_block_t *)(ptrTo(header, pwdAttr->iNode));
  if (getFileAttrByNum(header, fileNum, pwdAttr)->nlink != 1) { // update nlink
    pwdAttr->nlink--;
  }
  // Remove name
  int removedCount = removeFileName(header, fileNum, pwdAttr);

  // Remove Fileattr and shift File attr
  // Get starting dir block and setup shift
  size_t fileArrSize = sizeof(pwd->files) / sizeof(_file_attr_t);
  _file_attr_t *fileArray = pwd->files;
  _dir_overflow_block_t *pwdOverflow = NULL;
  int fileIndex = fileNum;
  if (fileIndex >= fileArrSize) {
    fileIndex -= fileArrSize;
    pwdOverflow = (_dir_overflow_block_t *)(ptrTo(header, pwd->overflow));
    fileArrSize = sizeof(pwdOverflow->files) / sizeof(_file_attr_t);
    while (fileIndex >= fileArrSize) {
      fileIndex -= fileArrSize;
      pwdOverflow =
          (_dir_overflow_block_t *)(ptrTo(header, pwdOverflow->overflow));
    }
    fileArray = pwdOverflow->files;
  }
  int nextFileIndex = fileIndex + 1;

  // Shift all fileAttr down one starting at our removed index
  _file_attr_t *currFileAttr, *nextFileAttr;
  int endReached = FALSE;
  do {
    currFileAttr = &fileArray[fileIndex];
    if (nextFileIndex >= fileArrSize) { // check for overflow
      if (fileArray == pwd->files) {    // still on pwd
        if (pwd->overflow == 0) {
          endReached = TRUE;
          copyFileAttr(currFileAttr, NULL);
        } else {
          pwdOverflow = (_dir_overflow_block_t *)(ptrTo(header, pwd->overflow));
          fileArray = pwd->files;
        }
      } else { // Already on overflow
        if (pwdOverflow->overflow == 0) {
          endReached = TRUE;
          copyFileAttr(currFileAttr, NULL);
        } else {
          pwdOverflow =
              (_dir_overflow_block_t *)(ptrTo(header, pwdOverflow->overflow));
          fileArray = pwdOverflow->files;
        }
      }
      fileArrSize = sizeof(pwdOverflow->files) / sizeof(_file_attr_t);
      nextFileIndex = 0;
    }
    if (!endReached) {
      nextFileAttr = &fileArray[nextFileIndex];
      // Copy data
      copyFileAttr(currFileAttr, nextFileAttr);
      // Have we reached the end?
      if (currFileAttr->nlink == 0) {
        endReached = TRUE;
        if (nextFileIndex == 0) {
          freeupDirOverflowBlock(header, pwdOverflow, pwdAttr);
        }
      } else { // change name offset
        currFileAttr->name -= removedCount;
        if (currFileAttr->name < 0) {
          currFileAttr->name += BLOCK_SIZE;
        }
      }
      fileIndex = nextFileIndex;
      nextFileIndex++;
    }
  } while (!endReached);
  pwd->fileCount--;
  if (pwd->fileCount == 0) {
    freeupBlock(header, pwd->filenames);
    freeupBlock(header, pwdAttr->iNode);
    pwdAttr->size -= BLOCK_SIZE * 2;
    pwdAttr->iNode = 0;
  }
}

/*
  Removes file name from directory names.
  Shifts all following names to remove gap.
  Removes file entry from directory file array.
  Shifts all following entries to remove gap.
  Frees blocks accordingly.
  FILE SIZE MUST BE ZERO!  Run decreaseFileSize first to free file data!
  If dir is now empty, frees dir blocks.
  Updates dir size accordingly.
*/
void removeFile(_fs_header_t *header, int fileNum, _file_attr_t *pwdAttr) {
  _dir_block_t *pwd = (_dir_block_t *)(ptrTo(header, pwdAttr->iNode));

  // Last file in dir
  if (pwd->fileCount == 1) {
    freeupBlock(header, pwd->filenames);
    pwd->filenames = 0;
    pwd->fileCount--;
    pwdAttr->size -= BLOCK_SIZE;
    if (pwdAttr->iNode != 1) { // Keep root iNode
      freeupBlock(header, pwdAttr->iNode);
      pwdAttr->iNode = 0;
      pwdAttr->size -= BLOCK_SIZE;
    }
    return;
  }

  // Remove node
  removeFileNode(header, fileNum, pwdAttr);

  return;
}
/* End of helper functions */

/* Implements an emulation of the stat system call on the filesystem
   of size fssize pointed to by fsptr.

   If path can be followed and describes a file or directory
   that exists and is accessable, the access information is
   put into stbuf.

   On success, 0 is returned. On failure, -1 is returned and
   the appropriate error code is put into *errnoptr.

   man 2 stat documents all possible error codes and gives more detail
   on what fields of stbuf need to be filled in. Essentially, only the
   following fields need to be supported:

   st_uid      the value passed in argument
   st_gid      the value passed in argument
   st_mode     (as fixed values S_IFDIR | 0755 for directories,
                                S_IFREG | 0755 for files)
   st_nlink    (as many as there are subdirectories (not files) for
   directories (including . and ..), 1 for files) st_size     (supported
   only for files, where it is the real file size) st_atim st_mtim

*/
int __myfs_getattr_implem(void *fsptr, size_t fssize, int *errnoptr, uid_t uid,
                          gid_t gid, const char *path, struct stat *stbuf) {
  _fs_header_t *header = (_fs_header_t *)(fsptr);
  if (createFilesystem(header, fssize) == -1) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (!validPath(path)) {
    *errnoptr = EINVAL;
    return -1;
  }
  _file_attr_t *foundFile = getFileAttr(header, errnoptr, path);
  if (foundFile == NULL) {
    *errnoptr = ENOENT;
    return -1;
  }
  stbuf->st_uid = uid;
  stbuf->st_gid = gid;
  if ((int)(foundFile->nlink) > 1) {
    stbuf->st_mode = DIR;
  } else {
    stbuf->st_mode = FILE;
  }
  stbuf->st_nlink = foundFile->nlink;
  stbuf->st_size = foundFile->size;
  stbuf->st_atim = foundFile->atim;
  stbuf->st_mtim = foundFile->mtim;
  return 0;
}

/* Implements an emulation of the readdir system call on the filesystem
   of size fssize pointed to by fsptr.

   If path can be followed and describes a directory that exists and
   is accessable, the names of the subdirectories and files
   contained in that directory are output into *namesptr. The . and ..
   directories must not be included in that listing.

   If it needs to output file and subdirectory names, the function
   starts by allocating (with calloc) an array of pointers to
   characters of the right size (n entries for n names). Sets
   *namesptr to that pointer. It then goes over all entries
   in that array and allocates, for each of them an array of
   characters of the right size (to hold the i-th name, together
   with the appropriate '\0' terminator). It puts the pointer
   into that i-th array entry and fills the allocated array
   of characters with the appropriate name. The calling function
   will call free on each of the entries of *namesptr and
   on *namesptr.

   The function returns the number of names that have been
   put into namesptr.

   If no name needs to be reported because the directory does
   not contain any file or subdirectory besides . and .., 0 is
   returned and no allocation takes place.

   On failure, -1 is returned and the *errnoptr is set to
   the appropriate error code.

   The error codes are documented in man 2 readdir.

   In the case memory allocation with malloc/calloc fails, failure is
   indicated by returning -1 and setting *errnoptr to EINVAL.

*/

int __myfs_readdir_implem(void *fsptr, size_t fssize, int *errnoptr,
                          const char *path, char ***namesptr) {
  _fs_header_t *header = (_fs_header_t *)(fsptr);
  if (createFilesystem(header, fssize) == -1) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (!validPath(path)) {
    *errnoptr = EINVAL;
    return -1;
  }
  _file_attr_t *foundDir = getFileAttr(header, errnoptr, path);
  if (foundDir == NULL) {
    *errnoptr = EBADF;
    return -1;
  }
  if (foundDir->nlink < 2) {
    *errnoptr = EBADF;
    return -1;
  }

  _dir_block_t *thisDir = (_dir_block_t *)(ptrTo(header, foundDir->iNode));
  if (thisDir->fileCount == 0) {
    return 0;
  }
  int fileCount = thisDir->fileCount;
  char **strArr = calloc(fileCount, sizeof(char *));
  if (strArr == NULL) {
    *errnoptr = ENOMEM;
    return -1;
  }
  *namesptr = strArr;
  for (int i = 0; i < fileCount; i++) {
    char *thisFileName = getFileNameToHeap(
        header, (_file_attr_t *)&((thisDir->files[i])), foundDir);
    if (thisFileName == NULL) {
      for (int j = 0; j < i; j++) {
        free(strArr[j]);
      }
      free(strArr);
      *errnoptr = ENOMEM;
      return -1;
    }
    strArr[i] = thisFileName;
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  foundDir->atim = ts;
  return fileCount;
}

/* Implements an emulation of the mknod system call for regular files
   on the filesystem of size fssize pointed to by fsptr.

   This function is called only for the creation of regular files.

   If a file gets created, it is of size zero and has default
   ownership and mode bits.

   The call creates the file indicated by path.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 mknod.

*/
int __myfs_mknod_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path) {
  _fs_header_t *header = (_fs_header_t *)(fsptr);
  if (createFilesystem(header, fssize) == -1) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (!validPath(path)) {
    *errnoptr = EINVAL;
    return -1;
  }
  if (path[strlen(path) - 1] == '/') {
    *errnoptr = EINVAL;
    return -1;
  }

  // Add new node
  if (addFile(header, errnoptr, FILE, path) == -1) {
    return -1;
  }

  return 0;
}

/* Implements an emulation of the unlink system call for regular files
   on the filesystem of size fssize pointed to by fsptr.

   This function is called only for the deletion of regular files.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 unlink.

*/

int __myfs_unlink_implem(void *fsptr, size_t fssize, int *errnoptr,
                         const char *path) {
  _fs_header_t *header = (_fs_header_t *)(fsptr);
  if (createFilesystem(header, fssize) == -1) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (!validPath(path)) {
    *errnoptr = EINVAL;
    return -1;
  }
  if (path[strlen(path) - 1] == '/') {
    *errnoptr = EINVAL;
    return -1;
  }
  _file_attr_t *foundFile = getFileAttr(header, errnoptr, path);
  if (foundFile == NULL) {
    *errnoptr = ENOENT;
    return -1;
  }
  if (foundFile->nlink > 1) {
    *errnoptr = EISDIR;
    return -1;
  }

  // truncate file to 0
  if (foundFile->size > 0) {
    decreaseFileSize(header, 0, foundFile);
  }

  // Get file's directory
  char pathBuf[strlen(path)];
  strcpy(pathBuf, path);
  // Split path into directory & filename
  char *fileName = strrchr(pathBuf, '/') + 1;
  *(fileName - 1) = '\0';

  // get directory and remove file
  _file_attr_t *pwdAttr;
  if (pathBuf[0] == '\0') { // At root
    pwdAttr = &(header->rtAttr);
  } else {
    pwdAttr = getFileAttr(header, errnoptr, pathBuf);
  }

  removeFile(header, getFileNumByName(header, fileName, pwdAttr), pwdAttr);

  return 0;
}

/* Implements an emulation of the rmdir system call on the filesystem
   of size fssize pointed to by fsptr.

   The call deletes the directory indicated by path.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The function call must fail when the directory indicated by path is
   not empty (if there are files or subdirectories other than . and ..).

   The error codes are documented in man 2 rmdir.

*/
int __myfs_rmdir_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path) {
  _fs_header_t *header = (_fs_header_t *)(fsptr);
  if (createFilesystem(header, fssize) == -1) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (!validPath(path)) {
    *errnoptr = EINVAL;
    return -1;
  }
  _file_attr_t *foundDir = getFileAttr(header, errnoptr, path);
  if (foundDir == NULL) {
    *errnoptr = ENOENT;
    return -1;
  }
  if (foundDir->nlink < 2) {
    *errnoptr = ENOTDIR;
    return -1;
  }
  if (foundDir->iNode != 0) {
    *errnoptr = ENOTEMPTY;
    return -1;
  }
  if (foundDir->iNode == 1) {
    *errnoptr = EPERM;
  }

  // Fix trailing '/'
  char pathBuf[strlen(path)];
  strcpy(pathBuf, path);
  while (pathBuf[strlen(pathBuf) - 1] == '/') {
    pathBuf[strlen(pathBuf) - 1] = '\0';
  }
  // Split path into directory & dirname
  char *dirName = strrchr(pathBuf, '/') + 1;
  *(dirName - 1) = '\0';

  // get directory and remove file
  _file_attr_t *pwdAttr;
  if (pathBuf[0] == '\0') { // At root
    pwdAttr = &(header->rtAttr);
  } else {
    pwdAttr = getFileAttr(header, errnoptr, pathBuf);
  }

  removeFile(header, getFileNumByName(header, dirName, pwdAttr), pwdAttr);

  pwdAttr->nlink--;

  return 0;
}

/* Implements an emulation of the mkdir system call on the filesystem
   of size fssize pointed to by fsptr.

   The call creates the directory indicated by path.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 mkdir.

*/
int __myfs_mkdir_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path) {
  _fs_header_t *header = (_fs_header_t *)(fsptr);
  if (createFilesystem(header, fssize) == -1) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (!validPath(path)) {
    *errnoptr = EINVAL;
    return -1;
  }

  // Add new node
  if (addFile(header, errnoptr, DIR, path) == -1) {
    return -1;
  }

  return 0;
}

/* Implements an emulation of the rename system call on the filesystem
   of size fssize pointed to by fsptr.

   The call moves the file or directory indicated by from to to.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   Caution: the function does more than what is hinted to by its name.
   In cases the from and to paths differ, the file is moved out of
   the from path and added to the to path.

   The error codes are documented in man 2 rename.

*/
int __myfs_rename_implem(void *fsptr, size_t fssize, int *errnoptr,
                         const char *from, const char *to) {
  // __myfs_rename_implem: several cases
  //   a) rename a file
  //   mv file new_file
  //   b) rename a directory
  //   mv dir new_dir
  //   c) move a file into another directory, which must exist
  //   c-1) But the file of that name does not exist in the other directory
  //   mv file ./dir/
  //   c-2) But the file exists in the other directory, in which case the
  //   existing file needs to go away first
  //   mv file ./dir/ but dir contains file
  //   d) move a directory into another directory, which does exist
  //   d-1) The subdirectory exists
  //   d-2) The subdirectory does not exist
  _fs_header_t *header = (_fs_header_t *)(fsptr);
  if (createFilesystem(header, fssize) == -1) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (!validPath(from) || !validPath(to)) {
    *errnoptr = EINVAL;
    return -1;
  }

  // Get 'from' and 'to'
  _file_attr_t *foundFrom = getFileAttr(header, errnoptr, from);
  if (foundFrom == NULL) {
    *errnoptr = ENOENT;
    return -1;
  }
  if (foundFrom->iNode == 1) { // Can't move root dir
    *errnoptr = EINVAL;
    return -1;
  }
  _file_attr_t *foundTo = getFileAttr(header, errnoptr, to);
  if (foundTo == foundFrom) { // Same file, do nothing
    return 0;
  }

  // Save types (FILE/DIR)
  mode_t fromType, toType;
  if (foundFrom->nlink == 1) {
    fromType = FILE;
  } else {
    fromType = DIR;
  }
  if (foundTo == NULL) {
    toType = 0;
  } else {
    // 'to' file is dir and not empty
    if (foundTo->nlink > 2) {
      *errnoptr = ENOTEMPTY;
    } else if (foundTo->nlink == 1) {
      toType = FILE;
    } else {
      toType = DIR;
    }
  }

  // Get working dir and filename for 'from'
  // Parse 'from' to get new location and name
  char fromBuf[strlen(from)];
  strcpy(fromBuf, from);
  // Remove trailing '/'
  while (fromBuf[strlen(fromBuf) - 1] == '/') {
    fromBuf[strlen(fromBuf) - 1] = '\0';
  }
  // Split from into directory & fileName
  char *fromFileName = strrchr(fromBuf, '/') + 1;
  *(fromFileName - 1) = '\0';
  // get directory
  _file_attr_t *fromDirAttr;
  if (fromBuf[0] == '\0') { // At root
    fromDirAttr = &(header->rtAttr);
  } else {
    fromDirAttr = getFileAttr(header, errnoptr, fromBuf);
  }

  // Get working dir and filename for 'to'
  // Parse 'to' to get new location and name
  char toBuf[strlen(to)];
  strcpy(toBuf, to);
  // Remove trailing '/'
  while (toBuf[strlen(toBuf) - 1] == '/') {
    toBuf[strlen(toBuf) - 1] = '\0';
  }
  // Split to into directory & fileName
  char *toFileName = strrchr(toBuf, '/') + 1;
  *(toFileName - 1) = '\0';
  // get directory
  _file_attr_t *toDirAttr;
  if (toBuf[0] == '\0') { // At root
    toDirAttr = &(header->rtAttr);
  } else {
    toDirAttr = getFileAttr(header, errnoptr, toBuf);
  }

  // 'to' dir doesn't exist
  if (toDirAttr == NULL) {
    *errnoptr = ENOENT;
    return -1;
  }
  // 'to' dir is file
  if (toDirAttr->nlink < 2) {
    *errnoptr = ENOTDIR;
    return -1;
  }

  // Check stuff and do the rename/move
  if (fromType == DIR && toType == FILE) {
    *errnoptr = EISDIR;
  } else if (fromType == FILE && toType == DIR) {
    *errnoptr = ENOTDIR;
  }

  // If destination doesn't exist, create it
  int newFileNum = getFileNumByName(header, toFileName, toDirAttr);
  if (foundTo == NULL) {
    newFileNum = addFileNode(header, toFileName, fromType, toDirAttr);
    if (newFileNum == -1) {
      *errnoptr = ENOMEM;
      return -1;
    }
  } else if (foundTo->size > 0) { // file exist, delete its data
    decreaseFileSize(header, 0, foundTo);
  }

  // Copy 'from' to 'to'
  _file_attr_t temp;
  copyFileAttr(&temp, foundFrom);
  removeFileNode(header, getFileNumByName(header, fromFileName, fromDirAttr),
                 fromDirAttr);
  foundTo = getFileAttr(header, errnoptr, to);
  // preserve new name offset and copy
  temp.name = foundTo->name;
  copyFileAttr(foundTo, &temp);

  return 0;
}

/* Implements an emulation of the truncate system call on the filesystem
   of size fssize pointed to by fsptr.

   The call changes the size of the file indicated by path to offset
   bytes.

   When the file becomes smaller due to the call, the extending bytes are
   removed. When it becomes larger, zeros are appended.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 truncate.

*/

int __myfs_truncate_implem(void *fsptr, size_t fssize, int *errnoptr,
                           const char *path, off_t offset) {
  _fs_header_t *header = (_fs_header_t *)(fsptr);
  if (createFilesystem(header, fssize) == -1) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (!validPath(path)) {
    *errnoptr = EINVAL;
    return -1;
  }
  _file_attr_t *foundFile = getFileAttr(header, errnoptr, path);
  if (foundFile == NULL) {
    return -1;
  }
  if (foundFile->nlink > 1) {
    *errnoptr = EISDIR;
    return -1;
  }

  if (foundFile->size < offset) {
    if (increaseFileSize(header, offset, foundFile) < 0) {
      if (foundFile->size == 0) {
        freeupBlock(header, foundFile->iNode);
        foundFile->iNode = 0;
      }
      *errnoptr = EINVAL;
      return -1;
    }
  } else if (foundFile->size > offset) {
    decreaseFileSize(header, offset, foundFile);
  }
  foundFile->size = (size_t)offset;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  foundFile->mtim = ts;
  return 0;
}

/* Implements an emulation of the open system call on the filesystem
   of size fssize pointed to by fsptr, without actually performing the opening
   of the file (no file descriptor is returned).

   The call just checks if the file (or directory) indicated by path
   can be accessed, i.e. if the path can be followed to an existing
   object for which the access rights are granted.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The two only interesting error codes are

   * EFAULT: the filesystem is in a bad state, we can't do anything

   * ENOENT: the file that we are supposed to open doesn't exist (or a
             subpath).

   It is possible to restrict ourselves to only these two error
   conditions. It is also possible to implement more detailed error
   condition answers.

   The error codes are documented in man 2 open.

*/
int __myfs_open_implem(void *fsptr, size_t fssize, int *errnoptr,
                       const char *path) {
  // Let's just let the getattr function handle this.
  // It already does our error checking and returns the value we want.
  uid_t uid = (uid_t)NULL;
  gid_t gid = (gid_t)NULL;
  struct stat stbuf;
  return __myfs_getattr_implem(fsptr, fssize, errnoptr, uid, gid, path, &stbuf);
}

/* Implements an emulation of the read system call on the filesystem
   of size fssize pointed to by fsptr.

   The call copies up to size bytes from the file indicated by
   path into the buffer, starting to read at offset. See the man page
   for read for the details when offset is beyond the end of the file etc.

   On success, the appropriate number of bytes read into the buffer is
   returned. The value zero is returned on an end-of-file condition.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 read.

*/

int __myfs_read_implem(void *fsptr, size_t fssize, int *errnoptr,
                       const char *path, char *buf, size_t size, off_t offset) {
  _fs_header_t *header = (_fs_header_t *)(fsptr);
  if (createFilesystem(header, fssize) == -1) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (!validPath(path)) {
    *errnoptr = EINVAL;
    return -1;
  }
  if ((int)offset + (int)size != offset + size) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (offset < 0) {
    *errnoptr = EFAULT;
    return -1;
  }
  _file_attr_t *foundFile = getFileAttr(header, errnoptr, path);
  if (foundFile == NULL) {
    *errnoptr = ENOENT;
    return -1;
  }
  if (foundFile->nlink > 1) {
    *errnoptr = EISDIR;
    return -1;
  }
  if (foundFile->size == 0) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    foundFile->atim = ts;
    return 0;
  }
  if (offset >= foundFile->size) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (offset + size > foundFile->size) {
    size = foundFile->size - offset;
  }

  int startingBlockIndex = offset / BLOCK_SIZE;
  _file_block_t *currINode =
      getFileINodeByBlockIndex(header, startingBlockIndex, foundFile);
  size_t blockArrSize = sizeof(currINode->data) / sizeof(block_t);
  startingBlockIndex %= blockArrSize;
  offset %= BLOCK_SIZE;
  readDataIntoBufferRecurs(header, buf, size, offset, startingBlockIndex,
                           currINode);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  foundFile->atim = ts;
  return (int)size;
}

/* Implements an emulation of the write system call on the filesystem
   of size fssize pointed to by fsptr.

   The call copies up to size bytes to the file indicated by
   path into the buffer, starting to write at offset. See the man page
   for write for the details when offset is beyond the end of the file etc.

   On success, the appropriate number of bytes written into the file is
   returned. The value zero is returned on an end-of-file condition.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 write.

*/
int __myfs_write_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path, const char *buf, size_t size,
                        off_t offset) {
  _fs_header_t *header = (_fs_header_t *)(fsptr);
  if (createFilesystem(header, fssize) == -1) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (!validPath(path)) {
    *errnoptr = EINVAL;
    return -1;
  }
  if ((int)offset + (int)size != offset + size) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (offset < 0) {
    *errnoptr = EFAULT;
    return -1;
  }
  _file_attr_t *foundFile = getFileAttr(header, errnoptr, path);
  if (foundFile == NULL) {
    *errnoptr = ENOENT;
    return -1;
  }
  if (foundFile->nlink > 1) {
    *errnoptr = EISDIR;
    return -1;
  }

  // Nothing to write
  if (size == 0) {
    return 0;
  }

  // Offset is beyond file size.  Increase file size to offset.
  // A hole of NULLs will be left in the file.
  if (offset > foundFile->size) {
    if (increaseFileSize(header, offset, foundFile) == -1) {
      *errnoptr = ENOSPC;
      return -1;
    }
    foundFile->size = offset;
  }

  int startingBlockIndex;
  _file_block_t *currINode;
  // File size is ZERO.  Need to initialize first iNode & datablock
  if (foundFile->iNode == 0) {
    startingBlockIndex = 0;
    if (header->freeBlocks < 2) {
      *errnoptr = ENOSPC;
      return -1;
    }
    foundFile->iNode = getFreeBlock(header);
    currINode = getFileINodeByBlockIndex(header, startingBlockIndex, foundFile);
    currINode->data[0] = getFreeBlock(header);
  } else { // File size > 0
    startingBlockIndex = offset / BLOCK_SIZE;
    currINode = getFileINodeByBlockIndex(header, startingBlockIndex, foundFile);
  }

  size_t blockArrSize = sizeof(currINode->data) / sizeof(block_t);
  startingBlockIndex %= blockArrSize;
  offset %= BLOCK_SIZE;
  int writeCount = writeDataIntoFileRecurs(header, buf, size, offset,
                                           startingBlockIndex, currINode);

  // We couldn't write anything at all, no space
  if (writeCount == 0 && size > 0) {
    *errnoptr = ENOSPC;
    return -1;
  }

  // Update filesize if needed
  if (offset + writeCount > foundFile->size) {
    foundFile->size = (size_t)(offset + writeCount);
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  foundFile->mtim = ts;
  foundFile->atim = ts;
  return writeCount;
}

/* Implements an emulation of the utimensat system call on the filesystem
   of size fssize pointed to by fsptr.

   The call changes the access and modification times of the file
   or directory indicated by path to the values in ts.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 utimensat.

*/
int __myfs_utimens_implem(void *fsptr, size_t fssize, int *errnoptr,
                          const char *path, const struct timespec ts[2]) {
  _fs_header_t *header = (_fs_header_t *)(fsptr);
  if (createFilesystem(header, fssize) == -1) {
    *errnoptr = EFAULT;
    return -1;
  }
  if (!validPath(path)) {
    *errnoptr = EINVAL;
    return -1;
  }
  _file_attr_t *foundFile = getFileAttr(header, errnoptr, path);
  if (foundFile == NULL) {
    *errnoptr = ENOENT;
    return -1;
  }
  foundFile->atim = ts[0];
  foundFile->mtim = ts[1];
  return 0;
}

/* Implements an emulation of the statfs system call on the filesystem
   of size fssize pointed to by fsptr.

   The call gets information of the filesystem usage and puts in
   into stbuf.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 statfs.

   Essentially, only the following fields of struct statvfs need to be
   supported:

   f_bsize   fill with what you call a block (typically 1024 bytes)
   f_blocks  fill with the total number of blocks in the filesystem
   f_bfree   fill with the free number of blocks in the filesystem
   f_bavail  fill with same value as f_bfree
   f_namemax fill with your maximum file/directory name, if your
             filesystem has such a maximum

*/
int __myfs_statfs_implem(void *fsptr, size_t fssize, int *errnoptr,
                         struct statvfs *stbuf) {
  _fs_header_t *header = (_fs_header_t *)(fsptr);
  if (createFilesystem(header, fssize) == -1) {
    *errnoptr = EFAULT;
    return -1;
  }
  stbuf->f_bsize = (unsigned long)(header->blockSize);
  stbuf->f_blocks = (fsblkcnt_t)(header->totalBlocks);
  stbuf->f_bfree = (fsblkcnt_t)(header->freeBlocks);
  stbuf->f_bavail = stbuf->f_bfree;
  return 0;
}
