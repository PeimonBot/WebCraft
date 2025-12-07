# WebCraft

> An async first networking library leveraging powerful features of C++23 built for scale, speed, and ease.

NOTE: This codebase is recommended to run on C++ 23 since most of the features are geared to it. If its possible try to get your compiler to compile with experimental libraries enabled (which will enable all the features the compiler may not have implemented for C++ 23).

The following compilers (and anything newer) are supported: GCC 13, MSVC 2022, Clang 17

## Features / Modules

1. Asynchronous Runtime, Supporting Yielding and working with timeouts that are not thread based
2. Basic networking API (TCP & UDP Sockets, async file read & write) via I/O streams.

## Contributing to it

1. Make a fork of the repository
2. Make sure your environment is set up (I use VS Code + Microsoft's C++ Extension Pack & CMake extensions). Ensure that a C++ compiler exists on your computer and is added to the PATH as well as cmake.
3. Check out to a new branch and give it a good name
4. Ensure `vcpkg` is setup. If `vcpkg` is empty then run this: `git submodule update --init --recursive`. Then enter `vcpkg` and run `bootstrap-vcpkg.bat` on Windows or `boostrap-vcpkg.sh` on Linux or MacOS.
5. Configure your project (If VS Code is setup with CMake then it should be automatically done for you. Otherwise, run `cmake --preset linux-build` if on Linux, `cmake --preset windows-build` if on Windows, `cmake --preset macos-build` on MacOS.
6. Build the library: `cmake -S . -B build -DWEBCRAFT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug` && `cmake --build build --config Debug`
7. Run the tests `ctest --test-dir build --output-on-failure --verbose`. To test a specific test, run ` ctest --test-dir build --output-on-failure --verbose -R "<<test regex>>"`
8. Develop! Your environment is set up. Add your changes, perform steps 6 & 7 to make sure that your changes don't break anything.
9. Once everything is properly checked. Push your changes to your fork and make a PR. Optionally: Get Copilot to review your changes before you get @adityarao2005 (me) to review it.
10. If everything checks out with your code on all platforms on the CI runner, then I'll merge the PR, and you'll have contributed to WebCraft. Otherwise, I'll mention specific comments and will require you to revise your work before requesting my review again and running another build.

To see what issues exist, check out the issues tab or checkout this link to see what I'm working on: [https://github.com/users/adityarao2005/projects/4](https://github.com/users/adityarao2005/projects/4).
If you plan on working on an issue, add a comment saying that you're working on it.

## Using it in your project

TBD. Currently, WebCraft does not have a port for vcpkg. If one of the contributors can add this library to vcpkg, that would be great (Here's how to do it. I think it should be intuitive enough: [https://learn.microsoft.com/en-gb/vcpkg/get_started/get-started-packaging?pivots=shell-bash](https://learn.microsoft.com/en-gb/vcpkg/get_started/get-started-packaging?pivots=shell-bash))!
