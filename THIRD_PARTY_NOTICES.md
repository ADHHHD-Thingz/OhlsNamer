# Third-party notices

## ols-parser-public

Files under `third_party/ols-parser-public/` are copied from [JSD-SpeedUP/ols-parser-public](https://github.com/JSD-SpeedUP/ols-parser-public), an independent C parser for WinOLS `.ols` projects.

The upstream project is distributed under the BSD Zero Clause License (0BSD), retained in `third_party/ols-parser-public/LICENSE`. These files are used as a development-time reference and are not yet the application's primary parsing implementation. Upstream interpretations are reverse-engineered, not an official EVC format specification.

## UnRAR 7.0.7

Files under `third_party/unrar/` are the UnRAR 7.0.7 source by Alexander L. Roshal (RARLAB). They come from the Debian/Ubuntu `unrar-nonfree_7.0.7.orig.tar.gz` package, SHA-256 `da95829c7e66fe461c06eb4bea8145e58d88d76909432d0875cd1ff86669f728`. The source is unmodified apart from removing the Visual Studio project files. OhlsNamer uses it only to read (decode) RAR archives through its DLL API (`ols_unrar.cpp`). Renaming entries in RAR archives is done by `ols_archive.hpp`, which was written from RARLAB's published header documentation and uses no UnRAR code. That writer edits headers and copies packed data unchanged; it never compresses data.

UnRAR license (full text in `third_party/unrar/license.txt`):

```
******    *****   ******   UnRAR - free utility for RAR archives
 **   **  **   **  **   **  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 ******   *******  ******    License for use and distribution of
 **   **  **   **  **   **   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 **   **  **   **  **   **         FREE portable version
                                   ~~~~~~~~~~~~~~~~~~~~~

      The source code of UnRAR utility is freeware. This means:

   1. All copyrights to RAR and the utility UnRAR are exclusively
      owned by the author - Alexander Roshal.

   2. UnRAR source code may be used in any software to handle
      RAR archives without limitations free of charge, but cannot be
      used to develop RAR (WinRAR) compatible archiver and to
      re-create RAR compression algorithm, which is proprietary.
      Distribution of modified UnRAR source code in separate form
      or as a part of other software is permitted, provided that
      full text of this paragraph, starting from "UnRAR source code"
      words, is included in license, or in documentation if license
      is not available, and in source code comments of resulting package.

   3. The UnRAR utility may be freely distributed. It is allowed
      to distribute UnRAR inside of other software packages.

   4. THE RAR ARCHIVER AND THE UnRAR UTILITY ARE DISTRIBUTED "AS IS".
      NO WARRANTY OF ANY KIND IS EXPRESSED OR IMPLIED.  YOU USE AT 
      YOUR OWN RISK. THE AUTHOR WILL NOT BE LIABLE FOR DATA LOSS, 
      DAMAGES, LOSS OF PROFITS OR ANY OTHER KIND OF LOSS WHILE USING
      OR MISUSING THIS SOFTWARE.

   5. Installing and using the UnRAR utility signifies acceptance of
      these terms and conditions of the license.

   6. If you don't agree with terms of the license you must remove
      UnRAR files from your storage devices and cease to use the
      utility.

      Thank you for your interest in RAR and UnRAR.


                                            Alexander L. Roshal
```
