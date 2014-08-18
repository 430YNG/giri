## Giri: Dynamic Program Slicing in LLVM

| Giri        | Dynamic Program Slicing in LLVM                               |
|:------------|:--------------------------------------------------------------|
| Author      | Swarup Kumar Sahoo, Mingliang Liu, John Criswell, Vikram Adve |
| License     | University of Illinois Open Source License                    |
| Version     | 3.4                                                           |

_Dynamic program slicing_ is a technique that can precisely determine which instructions affected a particular value in a single execution of a program.

### Usage

1. You can [compile Giri by yourself](https://github.com/liuml07/giri/wiki/How-to-Compile-Giri), or simply use the [Docker image](https://registry.hub.docker.com/u/liuml07/giri/)
2. [Hello World!](https://github.com/liuml07/giri/wiki/Hello-World)
3. [Example Usage](https://github.com/liuml07/giri/wiki/Example-Usage)

Please see our [wiki page](https://github.com/liuml07/giri/wiki/) for more information.

### Credit

This project was first developed by [Swarup Kumar Sahoo](http://web.engr.illinois.edu/~ssahoo2/), [John Criswell](http://www.bigw.org/~jcriswel), and [Dr. Vikram S. Adve](http://llvm.cs.uiuc.edu/~vadve/) from UIUC. It was selected by the Google Summer of Code **(GSoC)** 2013, under its umbrella project LLVM. [Mingliang Liu](http://pacman.cs.tsinghua.edu.cn/~liuml07) from Tsinghua University joined to improve Giri in June, 2013. It's an ongoing project and pull requests are heavily appreciated.

If you use Giri in your research project, please cite our work.

[1] Swarup Kumar Sahoo, John Criswell, Chase Geigle, and Vikram Adve. *Using Likely Invariants for Automated Software Fault Localization*.
In Proceedings of the 18th International Conference on Architectural Support for Programming Languages and Operating Systems, **ASPLOS**'13, New York, USA, 2013. ACM.
