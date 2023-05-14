# Minitar
This project is a simplified version of the tar utility, called minitar

The tar utility was originally developed back in the era of widespread archival storage on magnetic tapes – tar is in fact short for “tape archive.” Therefore, archive files created by tar have a very simple format that is easy to read and write sequentially.

While minitar doesn't replicate the full functionality of the original tar utility, it is still quite capable. It allows you to create to create archives from files, modify existing archives, and extract the original files back from an archive.

Minitar is fully Posix-compliant, meaning it can freely interoperate with all of the standard tape-archive utility programs like the original tar. In other words, minitar is able to read and manipulate archives generated by tar, and vice versa.

## What is in this directory?
<ul>
  <li>  <code>minitar_main.c</code> : Implements the command-line interface for the minitar application. Parses command-line arguments and invokes archive management functions.
  <li>  <code>minitar.h</code> : Header file declaring archive file management functions.
  <li>  <code>minitar.c</code> : Implementations of functions to perform various archive operations, such as creating archives, updating archives, or extracting data from archives.
  <li>  <code>file_list.h</code> : Header file for a linked list data structure used to store file names.
  <li>  <code>file_list.c</code> : Implementation of the linked list data structure for file names.
  <li>  <code>testius</code> : Script to run minitar test cases.
  <li>  <code>Makefile</code> : Build file to compile and run test cases.
  <li>  <code>test_cases</code> Folder, which contains:
    <ul>
      <li>  <code>input</code> : Input files used in automated testing cases
      <li>  <code>resources</code> : More input files
      <li>  <code>output</code> : Expected output
    </ul>  
</ul>

## Running Tests

A Makefile is provided as part of this project. This file supports the following commands:

<ul>
  <li>  <code>make</code> : Compile all code, produce an executable minitar program.
  <li>  <code>make clean</code> : Remove all compiled items. Useful if you want to recompile everything from scratch.
  <li>  <code>make clean-tests</code> : Remove all files produced during execution of the tests.
  <li>  <code>make test</code> : Run all test cases
  <li>  <code>make test testnum=5</code> : Run test case #5 only
</ul>
