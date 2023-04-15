# New File System For XV6
## 1.Introduction
This is a new file system for xv6. It is based on the original file system of xv6. We implement Ext3 [Extended File System](https://en.wikipedia.org/wiki/Extended_file_system) and Ext4 file system for xv6.
## 2.Features
+ support big files
+ support symbolic links
+ support file permissions
+ support inode bitmap
+ support file system meta information
+ support HTree(Hash Tree) directory index
+ support Extents Feature of Ext4
+ support file system recycle bin
+ support `lseek` and `append` operation

## 3.Usage
The operating environment of our team is the Windows Subsystem for Linux (WSL), the Ubuntu operating system, and the configuration process is as follows

### 3.1Setup

1. Install [Windows Subsystem for Linux](https://docs.microsoft.com/en-us/windows/wsl/install-win10) (WSL)

2. Install [Ubuntu 20.04 from the Microsoft Store](https://www.microsoft.com/en-us/p/ubuntu/9nblggh4msv6) on WSL.

3. Install the compilation toolchain in Ubuntu:

    1. ```
       $ sudo apt-get update && sudo apt-get upgrade
       $ sudo apt-get install git build-essential gdb-multiarch qemu-system-misc gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
       ```

4. Enter the xv6 decompressed directory, right-click to open with vscode, and enter in the shell

```
make qemu
```

==Note==: It will take a long time to run make qemu for the first time. This is because the file system image needs to be created. Since our team needs to implement large files, the created file system image is very large (300,000 blocks) , you should wait for 2-3 minutes when executing the output to the following paragraph, it is creating a file system image, please wait patiently for a while.

```
# Wait a while after outputting this line
nmeta 1587 (boot, super, log blocks 45, inode bitmap blocks 2, inode blocks 1501, bitmap blocks 37) blocks 298413 total 300000
```



Note: To exit the xv6 system under windows, press `ctrl`+`a` and release it, then press `X`

If you are not a windows operating system, such as macos, just refer to https://pdos.csail.mit.edu/6.828/2021/tools.html for the configuration process.

If you want to clear the compiled operating system

```
make clean
```

### 3.2Run

#### 3.2.1 Commands for large files

(Use it with caution, it will take a long time to run, it is recommended that you directly watch our screen recording file, so you can directly drag the progress bar to the end to see the result)

```
big
```

This is a test program, the user program big, which will continue to write content to a file before the set file size is reached, so as to test whether a file can reach a large enough size. During operation, the block counter will update the number of written blocks in real time on the command line, and report the test success after reaching the preset number.

#### 3.2.2 Soft link

```
test
symlinktest
```

There are two test files for soft links: `test` and `symlinktest`

- `test` is a simple test program that only tests the creation process of soft links. But the created file can be displayed by the `ls` command, and then you can continue to operate on the file to verify whether the soft link is successful (we demonstrate how to use the soft link in the screen recording file `soft link.mp4`).
- `symlinktest` is a comprehensive soft link test, which includes almost all possible problems in the process of creating, modifying and deleting soft links. This test function comes from the xv6 official github repository: https://github.com/mit-pdos/xv6-riscv-fall19/blob/xv6-riscv-fall19/user/symlinktest.c

#### 3.2.3 Commands for file access permissions

```
chmode [filename] [0/1/2/3]
```

The second parameter: 0 means unreadable and unwritable

1 means not readable and writable

2 means only readable but not writable

3 means both readable and writable

```
chspmode [filename] [admin password] [0/1]
```

Administrator identity password input `iam@admin9876`

The third parameter 0 means it is set as a normal file, and 1 means it is set as a sensitive protected file

#### 3.2.4 Recycle bin commands

```
delete [file]//delete file
```

```
refresh [file]//restore file
```

```
recyclelist//Display the files in the recycle bin
```

```
Files in the delete recycle bin can be permanently deleted
```

#### 3.2.5 View system free space status

```
fsinfo
```

#### 3.2.6 lseek positioning read and write position and append

```
lseektest
```

This command will test the lseek function, and the test results can be seen in section 3.8.

The function of append can be used by the relocation symbol `>>` to append characters to the text file.

```
echo [string] >> test.txt
```

#### 3.2.7 Commands of directory search tree Htree

```
search_test
```

This command will create more than 150 files and perform sequential access and random access, and output the running time. See section 3.5 for the running results.

#### 3.2.8 Commands for extents performance testing

This command will test the speed comparison between ext3's multi-level link index and ext4's extents. The first parameter is the number of blocks to be tested. Note that the effect of extents is not obvious when the number of blocks is small, and a relatively large number of blocks is required, but it takes a long time (30 minutes for a comparison of 50,000 blocks). You can see our screen recording file `video/ extenttest.wav`, so that you can directly drag the progress bar to the end to see the result.

```
extentstest2 [number of blocks]
```

#### 3.2.9 Inode bitmap commands

```
ibmaptest [number of inodes]
```

The first argument is the number of allocation `inode` to test. will output the time spent allocating the specified number of `inode` to create an empty file.

In order to facilitate your comparison, we have recorded a video `demo or test video/comparison_old_xv6_ibmap_500.mp4` of the time it takes for `ibmaptest` to allocate 500 `inode` on the original xv6 system.

#### 3.2.10 Commands for usertests

xv6 comes with the `usertests.c` file, which can test whether there are bugs in all aspects of the operating system. After implementing the new functions and modifications, we passed its `usertests`, indicating that we did not destroy the original functions. .

```
usertests
```

## 4. Implement Details
See `doc/file_system_for_xv6.md` for details.

## 5. Demo or test video

See  `demo and test video` directory.

## 6. Author and Contact

+ Qi  Ma(马骐) [:envelope:email](mq19@mails.tsinghua.edu.cn)  
+ Leyi  Pan(潘乐怡 )
+ Ao Sun(孙骜 )
+ Peiran  Xu(徐霈然)

## 7. Reference

1. MIT's 6.828 course is used to test the official test file of the large file function https://pdos.csail.mit.edu/6.828/2018/homework/big.c

2. Soft link test file symlinktest.c of xv6 official Github repository: https://github.com/mit-pdos/xv6-riscv-fall19/blob/xv6-riscv-fall19/user/symlinktest.c

3. Daniel Phillips proposed the HTree algorithm paper

    Phillips, Daniel. "A Directory Index for {EXT2}." 5th Annual Linux Showcase & Conference (ALS 01). 2001. https://archive.ph/20130415075208/http://www.linuxshowcase.org/2001/full_papers /phillips/phillips_html/index.html#selection-99.0-99.26

4. Blog: Analysis of EXT3 Directory Indexing Mechanism https://oenhan.com/ext3-dir-hash-index

5. Extents feature: https://github.com/RitwikDiv/xv6-PA4

## License

MIT License