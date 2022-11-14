# Lab work: Implementing redirections, pipeline, background execution.
Authors (team): <a href="https://github.com/bogdanmagometa">Bohdan Mahometa</a>, 
<a href="https://github.com/viktorpovazhuk">Viktor Povazhuk</a>,
<a href="https://github.com/shevdan">Bohdan Shevchuk</a>
<br>

Currently, the project is expected to work on Linux and MacOS.

## Prerequisites

Tools:
- cmake, GCC

Libraries:
- boost collection (`program_options`, `system` and `filesystem` libraries are used in the project)
- readline

Installing readline on Ubuntu:
```bash
sudo apt install libreadline-dev
```

### Compilation

#### Compile with `./compile.sh`

Execute the following command in the project root directory:
```bash
$ ./compile.sh
```

The executable will be built and copied to `./bin/myshell`.

#### Compile manually with cmake

Run the following in the project root directory:
```bash
$ mkdir build
$ cmake -S . -B build
$ cmake --build build
```

The executable will be available by the path `./build/myshell`.

### Installation

Assuming `./build` is the directory with compiled `myshell` and `./install` is the install directory:
```bash
$ mkdir -p ./install
$ cp ./build/myshell ./install/myshell
$ mkdir -p ./install/utils
$ cp ./build/utils/mycat ./install/utils/mycat
```

### Usage

Assuming the `myshell` executable is available by the path `./bin/myshell`:
```bash
$ mkdir -p data
$ echo "echo hi" > ./data/hi_script.msh
$ ./bin/myshell ./data/hi_script.msh
hi
$ mexport VAR=$(mecho Hello)
$ mecho $VAR
Hello

```

### Important!

### Results

We added redirections, pipeline and background execution to myshell.

# Additional tasks
Not yet.
