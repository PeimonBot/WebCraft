/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "WebCraft", "index.html", [
    [ "Asynchronous I/O in WebCraft", "md_include_2webcraft_2async_2io_2README.html", [
      [ "Async Streams", "md_include_2webcraft_2async_2io_2README.html#autotoc_md1", [
        [ "Async readable streams", "md_include_2webcraft_2async_2io_2README.html#autotoc_md2", null ],
        [ "Async writable streams", "md_include_2webcraft_2async_2io_2README.html#autotoc_md3", null ],
        [ "Async stream helpers", "md_include_2webcraft_2async_2io_2README.html#autotoc_md4", null ],
        [ "Channels", "md_include_2webcraft_2async_2io_2README.html#autotoc_md5", null ]
      ] ],
      [ "Async Readable Stream Adaptors", "md_include_2webcraft_2async_2io_2README.html#autotoc_md6", [
        [ "Stream Adaptors Implementation", "md_include_2webcraft_2async_2io_2README.html#autotoc_md7", [
          [ "Transform adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md8", null ],
          [ "Map adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md9", null ],
          [ "Pipe adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md10", null ],
          [ "Filter adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md11", null ],
          [ "Limit adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md12", null ],
          [ "Skip adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md13", null ],
          [ "Take while adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md14", null ],
          [ "Drop while adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md15", null ],
          [ "Collect adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md16", [
            [ "Reduce collector", "md_include_2webcraft_2async_2io_2README.html#autotoc_md17", null ],
            [ "Joining collector", "md_include_2webcraft_2async_2io_2README.html#autotoc_md18", null ],
            [ "To Vector collector", "md_include_2webcraft_2async_2io_2README.html#autotoc_md19", null ],
            [ "Group By collector", "md_include_2webcraft_2async_2io_2README.html#autotoc_md20", null ]
          ] ],
          [ "Forward To adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md21", null ],
          [ "Min adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md22", null ],
          [ "Max adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md23", null ],
          [ "Sum adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md24", null ],
          [ "Find first adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md25", null ],
          [ "Find last adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md26", null ],
          [ "Any matches adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md27", null ],
          [ "All matches adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md28", null ],
          [ "None matches adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md29", null ]
        ] ],
        [ "Some of the adaptors are planned to be implemented in this framework:", "md_include_2webcraft_2async_2io_2README.html#autotoc_md30", [
          [ "Sorted adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md31", null ],
          [ "Zip adaptor", "md_include_2webcraft_2async_2io_2README.html#autotoc_md32", null ]
        ] ]
      ] ],
      [ "Async File I/O", "md_include_2webcraft_2async_2io_2README.html#autotoc_md33", [
        [ "File Operations Table", "md_include_2webcraft_2async_2io_2README.html#autotoc_md34", null ]
      ] ],
      [ "Async Socket I/O", "md_include_2webcraft_2async_2io_2README.html#autotoc_md35", [
        [ "TCP Sockets Table", "md_include_2webcraft_2async_2io_2README.html#autotoc_md36", null ],
        [ "TCP Listeners Table", "md_include_2webcraft_2async_2io_2README.html#autotoc_md37", null ],
        [ "UDP Datagram Sockets", "md_include_2webcraft_2async_2io_2README.html#autotoc_md38", null ]
      ] ],
      [ "Planned implementation:", "md_include_2webcraft_2async_2io_2README.html#autotoc_md39", null ],
      [ "Implementation Details", "md_include_2webcraft_2async_2io_2README.html#autotoc_md40", null ]
    ] ],
    [ "Async Runtime Package: webcraft::async", "md_include_2webcraft_2async_2README.html", [
      [ "Awaitable Concepts", "md_include_2webcraft_2async_2README.html#autotoc_md42", null ],
      [ "Task<T>", "md_include_2webcraft_2async_2README.html#autotoc_md43", [
        [ "Promise Type Specification", "md_include_2webcraft_2async_2README.html#autotoc_md44", null ]
      ] ],
      [ "Synchronization Primitives", "md_include_2webcraft_2async_2README.html#autotoc_md45", [
        [ "event_signal", "md_include_2webcraft_2async_2README.html#autotoc_md46", null ],
        [ "async_event", "md_include_2webcraft_2async_2README.html#autotoc_md47", null ]
      ] ],
      [ "Task Completion and Control", "md_include_2webcraft_2async_2README.html#autotoc_md48", [
        [ "task_completion_source<T>", "md_include_2webcraft_2async_2README.html#autotoc_md49", null ],
        [ "fire_and_forget_task", "md_include_2webcraft_2async_2README.html#autotoc_md50", null ]
      ] ],
      [ "Combinators and Utilities", "md_include_2webcraft_2async_2README.html#autotoc_md51", [
        [ "when_all", "md_include_2webcraft_2async_2README.html#autotoc_md52", null ],
        [ "when_any", "md_include_2webcraft_2async_2README.html#autotoc_md53", null ],
        [ "sync_wait", "md_include_2webcraft_2async_2README.html#autotoc_md54", null ]
      ] ],
      [ "Generators", "md_include_2webcraft_2async_2README.html#autotoc_md55", [
        [ "generator<T>", "md_include_2webcraft_2async_2README.html#autotoc_md56", null ],
        [ "async_generator<T>", "md_include_2webcraft_2async_2README.html#autotoc_md57", null ]
      ] ],
      [ "Thread Pool", "md_include_2webcraft_2async_2README.html#autotoc_md58", null ],
      [ "Macros and Type Aliases", "md_include_2webcraft_2async_2README.html#autotoc_md59", null ]
    ] ],
    [ "Namespaces", "namespaces.html", [
      [ "Namespace List", "namespaces.html", "namespaces_dup" ],
      [ "Namespace Members", "namespacemembers.html", [
        [ "All", "namespacemembers.html", null ],
        [ "Functions", "namespacemembers_func.html", null ],
        [ "Typedefs", "namespacemembers_type.html", null ],
        [ "Enumerations", "namespacemembers_enum.html", null ]
      ] ]
    ] ],
    [ "Concepts", "concepts.html", "concepts" ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", null ],
        [ "Variables", "functions_vars.html", null ],
        [ "Typedefs", "functions_type.html", null ],
        [ "Related Symbols", "functions_rela.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", null ],
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"adaptors_8hpp.html",
"classwebcraft_1_1async_1_1io_1_1socket_1_1tcp__wstream.html#a8d38dd95d1ec9723bb489e27a4962a03",
"conceptwebcraft_1_1async_1_1io_1_1async__buffered__writable__stream.html",
"structwebcraft_1_1async_1_1io_1_1socket_1_1connection__info.html"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';