# FAT 16

### Introduction

This project was made for the Operating Systems class lectured by the Dr. Rafael Sachetto Oliveira in 2019/1 period of the Computing Science at the Federal University of São João del-Rei.

The main goal of this project was to implement the 16-bit FAT and a basic shell to navigate through file system.

The whole FAT file system should be contained in a single file named `fat.part`.

The specification of the FAT is:

* 512 bytes by sector
* 1024 bytes cluster
* 4096 clusters

Therefore, this FAT can store a raw data size of 4.194.304 bytes (4 MiB).

The commands implemented in the shell are:

Command | Effect
------------ | -------------
init | Initialize the file system (creates the `fat.part` file) or resets it.
load | Load the FAT table (only) to the program memory.
ls [PATH/DIR] | List the DIR directory. If DIR is a file, an error message is shown.
mkdir [PATH/DIR] | Creates a directory with DIR name, if any of the PATH parts are not existant, the program creates it. If the DIR directory already exists (either as a file or a directory), an error message is shown.
create [PATH/FILE] | Creates a file with FILE name, if FILE already exists (either as a file or a directory), an error message is shown.
unlink [PATH/FILE] | Deletes a file or a directory with FILE name. If FILE does not exists, an error message is shown.
write "STRING" [PATH/FILE] | Writes (overwriting) STRING in the FILE file. If FILE does not exists as a file or is a directory, an error message is shown.
append "STRING" [PATH/FILE] | Writes (appending) STRING in the FILE file. If FILE does not exists as a file or is a directory, an error message is shown.
read [PATH/FILE] | Prints in the standard output the contents of the FILE file. If FILE does not exists as a file or is a directory, an error message is shown.

### Compiling & Running

In order to compile this program (considering that you have the `make` tool installed), just type in your terminal:

```
$ make
```

And to run the FAT program:

```
$ ./fat
```
