# Templight 2.0 - Template Instantiation Profiler and Debugger

Templight is a Clang-based tool to profile the time and memory consumption of template instantiations and to perform interactive debugging sessions to gain introspection into the template instantiation process.

## Templight Profiler

The templight profiler is intended to be used as a drop-in substitute for the clang compiler (or one of its variants, like clang-cl) and it performs a full compilation, *honoring all the clang compiler options*, but records a trace (or history) of the template instantiations performed by the compiler for each translation unit.

The profiler will record a time-stamp when a template instantiation is entered and when it is exited, which is what forms the compilation-time profile of the template instantiations of a translation unit. The total memory consumption can also be recorded at these points, which might skew the time profiles slightly, but provide a profile of memory consumed by the compiler during the template instantiations.

Currently, templight provides the following output formats:

 - "yaml": Output in a YAML format, which is a simple text-based markup language.
 - "xml": Output in an XML format, the well-known text-based markup language.
 - "text": Output in a simple text file.

The profiler is enabled by the templight option `-profiler`, and it supports the following additional templight options:

 - `-stdout` - Output template instantiation traces to standard output.
 - `-memory` - Profile the memory usage during template instantiations.
 - `-safe-mode` - Output Templight traces without buffering, not to lose them at failure (note: this will distort the timing profiles due to file I/O latency).
 - `-ignore-system` - Ignore any template instantiation located in system-includes (-isystem), such as from the STL.
 - `-output=<string>` - Write Templight profiling traces to <file>. By default, it outputs to "current_source.cpp.trace.<format-extension>" or "current_source.cpp.memory.trace.<format-extension>" (if `-memory` is used).
 - `-format=<string>` - Specify the format of Templight outputs (yaml/xml/text, default is yaml).

## Templight Debugger

The templight interactive debugger is also a drop-in substitute for the clang compiler, honoring all its options, but it will interrupt the compilation with a console prompt (stdin). The operations of the templight debugger are modeled after the GDB debugger and essentially works the same, *with most commands being the same*, but instead of stepping through the execution of the code (in GDB), it steps through the instantiation of the templates.

In short, the templight debugger is to template meta-programming what GDB is to regular code.

The debugger is enabled by the templight option `-debugger`, and it supports the following additional templight options:

 - `-ignore-system` - Ignore any template instantiation located in system-includes (-isystem), such as from the STL.

**NOTE**: Both the debugger and the profiler **can be used together**. Obviously, if compilation is constantly interrupted by the interactive debugger, the time traces recorded by the profiler will be meaningless. Nevertheless, running the profiler during debugging will have the benefit of recording the history of the template instantiations, for later review, and can also record, with reasonably accuracy, the memory consumption profiles.

## Getting Started

### Getting and Compiling Templight

Templight must be compiled from source, alongside the Clang source code.

1. [Follow the instructions from LLVM/Clang](http://clang.llvm.org/get_started.html) to get a local copy of the Clang source code.

2. Clone the templight repository into the clang directories, as follows:
```bash
(from top-level folder)
$ cd llvm/tools/clang/tools
$ mkdir templight
$ git clone <link-to-clone-templight-github-repo> templight
```

3. Add the templight directory to the `CMakeLists.txt` file. 
  1. Open the file: `llvm/tools/clang/tools/CMakeLists.txt`
  2. Add the line: `add_subdirectory(templight)`
  3. Save and exit the editor.

4. Apply the supplied patch to Clang's source code:
```bash
(from top-level folder)
$ cd llvm/tools/clang
$ svn patch tools/templight/templight_clang_patch.diff
```

5. (Re-)Compile LLVM / Clang:
```bash
(from top-level folder)
$ mkdir build
$ cd build
$ cmake ../llvm/
$ make
```

6. If successful, there should be `templight` executables in the `build/bin` folder.

### Invoking Templight

Templight is designed to be a drop-in replacement for the clang compiler. This is because in order to correctly profile the compilation, it must run as the compiler would, honoring all compilation options specified by the user.

Because of this particular situation, the options for templight must be specially marked with `-Xtemplight` to distinguish them from other Clang options. The general usage goes like this:

```bash
$ templight [[-Xtemplight [templight-option]]|[clang-option]] <inputs>
```

For example, if we have this simple command-line invocation of clang:

```bash
$ clang++ -Wall -c some_source.cpp -o some_source.o
```

The corresponding templight profiler invocation, with options `-memory` and `-ignore-system` would look like this:

```bash
$ templight -Xtemplight -profiler -Xtemplight -memory -Xtemplight -ignore-system -Wall -c some_source.cpp -o some_source.o
```

Note that the order in which the templight-options appear is not important, and that clang options can be interleaved with templight-options. However, **every single templight-option must be immediately preceeded with `-Xtemplight`**.

As can be seen from that simple example, one can easily substitude the clang command (e.g., `clang++`) with a compound templight invocation (e.g., `templight -Xtemplight -profiler -Xtemplight -memory`) and thus, leave the remainder of the command-line intact. That is how templight can be used as a drop-in replacement for clang in any given project, build script or an IDE's build configuration.

If you use CMake and clang with the following common trick:
```bash
$ export CC=/path/to/llvm/build/bin/clang
$ export CXX=/path/to/llvm/build/bin/clang++
$ cmake <path-to-project-root>
```
Then, templight could be swapped in by the same trick:
```bash
$ export CXX="/path/to/llvm/build/bin/templight -Xtemplight -profiler -Xtemplight -memory"
$ cmake <path-to-project-root>
```
But be warned that the **cmake scripts will not recognize this compiler**, and therefore, you will have to make changes in the CMake files to be able to handle it. If anyone is interested in creating some CMake modules to deal with templight, please contact the maintainer, such modules would be more than welcomed.

### Invoking the Templight Profiler

The templight profiler is invoked by specifying the templight-option `-profiler`. By default, if no other templight-option is specified, the templight profiler will output only time profiling data, will use the "yaml" format, and will output to one file per input source file and will append the extension `.trace.yaml` to that file.

For example, running the following:
```bash
$ templight -Xtemplight -profiler -c some_source.cpp
```
will produce a file called `some_source.cpp.trace.yaml` in the same directory as `some_source.cpp`.

It is, in general, highly recommended to use the `-ignore-system` option for the profiler (and debugger too) because instantiations from standard or third-party libraries can cause significant noise and dramatically increase the size of the trace files produced. If libraries like the STL or most of Boost libraries are used, it is likely that most of the traces produced relate to those libraries and not to your own. Therefore, it is recommended to use `-isystem` when specifying the include directories for those libraries and then use the `-ignore-system` templight-option when producing the traces.

*Warning*: Trace files produced by the profiler can be very large, especially in template-heavy code. So, use this tool with caution. It is not recommended to blindly generate templight trace files for all the source files of a particular project (e.g., using templight profiler in place of clang in a CMake build tree). You should first identify specific parts of your project that you suspect (or know) lead to template instantiation problems (e.g., compile-time performance issues, or actual bugs or failures), and then create a small source file that exercises those specific parts and produce a templight trace for that source file.

### Using the Templight Debugger

The templight debugger is invoked by specifying the templight-option `-debugger`. When the debugger is launched, it will interrupt the compilation with a console command prompt, just like GDB. The templight debugger is designed to work in a similar fashion as GDB, with many of the same familiar commands and behavior.

Here is a quick reference for the available commands:

`break <template name>`
`b <template name>`
This command can be used to create a break-point at the instantiation (or any related actions, such as argument deductions) of the given template class or function. If the base name of the template is used, like `my_class`, then the compilation will be interrupted at any instantiation in which that base name appears. If an specialization for the template is used, like `my_class<double>`, then the compilation will be interrupted only when this specialization is encountered or instantiated.

`delete <breakpoint index>`
`d <breakpoint index>`
This command can be used to delete a break-point that was previously created with the `break` command. Each break-point is assigned an index number upon creation, and this index number should be used to remove that break-point. Use the `info break` (or `i b`) command to print the list of existing break-points so that the break-point index number can be retrieved, if forgotten.

`run` / `r`
`continue` / `c`
This command (in either forms or their short-hands) will resume compilation until the next break-point is encountered.

`kill` / `k`
`quit` / `q`
This command (in either forms or their short-hands) will resume compilation until the end, ignoring all break-points. In other words, this means "finish the compilation without debugging".

`step` / `s`
This command will step into the template instantiation, meaning that it will resume compilation until the next nested (or dependent) template is instantiated. For example, when instantiating a function template definition, function templates or class templates used inside the body of that function will be instantiated as part of it, and the `step` command allows you to step into those nested instantiations.

`next` / `n`
This command will skip to the end of the current template instantiation, and thus, skipping all nested template instantiations. The debugger will interrupt only when leaving the current instantiation context, or if a break-point is encountered within the nested template instantiations.

`where` / `backtrace` / `bt`
This command can be used to print the current stack of nested template instantiations.

`info <kind>` / `i <kind>`
This command can be used to query information about the state of the debugging session. The `<kind>` specifies one of the following options:
 - `frame` / `f`: Prints the information about the current template instantiation that the debugger is currently interrupted at.
 - `break` / `b`: Prints the list of all existing break-points.
 - `stack` / `s`: This is equivalent to calling `where` / `backtrace` / `bt` directly.

`lookup <identifier>`
`l <identifier>`
This command can be used to perform a look-up operation for a given identifier, within the context of the current template instantiation. This will print out the match(es) that the compiler would make to the given identifier. For example, if you are in the instantiation of a member function of an STL container and lookup an identifier like `value_type`, it will point you to the location of that the declaration that the compiler will associate to that identifier in that context (presumably a nested typedef in the STL container class template). If the identifier refers to a compile-time constant value (e.g., integral constant template argument), this command will also print its value.

`typeof <identifier>`
`t <identifier>`
This command is similar to `lookup` except that instead of showing you the location of the matching identifier (e.g., showing you a nested typedef or a data member) it will show you its type. If the identifier matches a variable, data member, function, or compile-time constant value, this command will show you the type of that object. If the identifier matches a type, it will show you what the actual type (fully instantiated) is. For example, if it matches a typedef or an alias, then the underlying type that it aliases will be shown (e.g., say you are inside the instantiation of the `std::vector<double>::insert` function and issue `typeof value_type`, it should show you `double` as a result).

*Warning*: The templight debugger is still in a *very experimental* phase. You should expect that some of the behaviors mentioned in the above descriptions of the commands will not work as advertized, yet. If you observe any unusual or buggy behaviors, please notify the maintainer and provide an example source file with the sequence of commands that led to the observed behavior.


## Inspecting the profiles

Currently, there is no up-to-date or actively maintained application to view or inspect the profiles.

Any contribution or work towards such an application is more than welcomed! The formats of the traces being text-based and using fairly standard markup languages will hopefully facilitate the development of such applications.

The [Templar application](https://github.com/schulmar/Templar) was one such early attempt at writing such an application.

## Credits

The original version of Templight was created by Zoltán Borók-Nagy, Zoltán Porkoláb and József Mihalicza:
http://plc.inf.elte.hu/templight/



