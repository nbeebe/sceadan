Sceadan v 1.2.1
===============

Sceadan (pronounced "scee-den") is a Systematic Classification Engine for Advanced Data ANalysis tool.

Sceadan is originally Old English / Proto-Germanic for "to classify."

	Copyright (c) 2012-2013 The University of Texas at San Antonio

**License: GPLv2**

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

**Written by:** 

Dr. Nicole Beebe (nicole.beebe@utsa.edu) and Lishu Liu, Department of
Information Systems and Cyber Security, and Laurence Maddox,
Department of Computer Science, University of Texas at San Antonio,
One UTSA Circle, San Antonio, Texas 78209

Simson L. Garfinkel (simsong@acm.org)
Naval Postgraduate School
Arlington, VA 22201


Purpose
-------

Sceadan is a C module that uses libsvm to determine the "type" of bulk
data based on file content, without using magic numbers, internal data
structures, or file system metadata. Currently this is done by
extracting features from the bulk data and applying a statical model
based on a support vector machine. Current veatures include unigram
counts, bigram counts, and other statistical measures.

The sceadan module ships with an application called `sceadan_app` that demonstrates the use of the program.

Layout
-------
$ROOT/DATA - The training data. Extension is assumed to be the
                   file type. Can contain subdirectories
$ROOT/src        - Sceadan source code
$ROOT/src/sceadan_app - generates vectors
$ROOT/tools/     - This directory
$ROOT/tools/Makefile - runs all training
$ROOT/tools/sceadan_train.py - Python program run by Makefile

Build Instructions
==================================

Linux build
-----------------

    git clone --recursive https://github.com/nbeebe/sceadan.git

###### Creating the liblinear dependency package
    
    cd <path_to>/sceadan/liblinear
    libtoolize
    autoheader
    aclocal
    autoconf
    automake -a
    ./configure
    make
    make install
    cd ..
    
###### Building sceadan
    
    cd <path_to>/sceadan
    libtoolize
    autoheader
    aclocal
    autoconf
    automake -a
    ./configure
    make
    make install
		
###### Building bulk_extractor with sceadan as a plugin
    
    git clone --recursive https://github.com/simsong/bulk_extractor.git
    cd bulk_extractor
    libtoolize
    autoheader -f
    aclocal -I m4
    autoconf -f
    libtoolize
    automake --add-missing --copy
    ./configure --with-sceadan=<sceadan directory>
    make
	sudo make install

Windows 64-bit build
-----------------

    git clone --recursive https://github.com/nbeebe/sceadan.git

###### Creating the liblinear dependency package
    
    cd <path_to>/sceadan/liblinear
    libtoolize
    autoheader
    aclocal
    autoconf
    automake -a
    mingw64-configure
    make

###### Building sceadan
    
    cd <path_to>/sceadan
    libtoolize
    autoheader -f
    aclocal -I m4
    autoconf -f
    libtoolize
    automake --add-missing --copy
    mingw64-configure
    make

###### bulk_extractor
    
    git clone --recursive https://github.com/simsong/bulk_extractor.git
    cd bulk_extractor
    libtoolize
    autoheader -f
    aclocal -I m4
    autoconf -f
    libtoolize
    automake --add-missing --copy
    mingw64-configure --with-sceadan=<sceadan directory> --disable-afflib --disable-libewf
    make

Tips for recompiling
---------------------

If you use the make file generated from CONFIGURE\_F20.bash, it will blow away everything with `make distclean` every time you build bulk extractor!

Re-build sceadan the smarter way if making dev changes to it or bulk_extractor:

    cd ../sceadan; make clean; mingw64-configure; make
    cd /home/deflogix/bulk_extractor; make distclean (to clean stuff up)
    cd win64; mingw64-configure --with-sceadan=../../sceadan --disable-afflib --disable-libewf

To test an updated sceadan in bulk extractor, go to the sceadan source dir (sceadan/src/\*.cpp), make a change, and type `make`. It should only rebuild the modified files. However, to relink the new sceadan to bulk extractor, you *need* to delete **bulk_extractor/win64/src/scan_sceadan.o**. Then type `make`. You can also update bulk extractor source files and type `make` in this directory to rebuild bulk extractor with the new changes.

Running bulk extractor
----------------------

The basic syntax for running bulk extractor with sceadan on both Windows and Linux is:

`bulk_extractor -E sceadan -S sceadan_model_file=<model file location> -o <log file location> <disk image location>`

Usage
-----

	src/sceadan_app [options] <target> 
        python3 tools/sceadan_train.py <args>

If `target` is a directory, Sceadan will operate on each file within
the directory individual, otherwise Sceadan will classify the single
file referenced.


Training Guide
--------------

Sceadan comes with a pre-trained model that is compiled into the
program. The model is generated by `liblinear` and then compiled into
C code with the program `src/mcompile.cpp`. This produces a `.c`
output file called `src/sceadan_model_precompiled.c` which is compiled
by the C compiler.

However, you may wish to train your own model. For example, you can:

* Experiment with different block sizes
* Experiment with different features 
* Train on different document types

To train sceadan you will use the files in the `tools/` directory. Please see the file `doc/training_procedure.md` for further information.


Machine Learning Science
====
Sceadan uses the default support vector machine model:

* Linear kernel function 
* L2 regularized L2 loss function, primal solver (Liblinear solver_type L2R_L2LOSS_SVC) (e=0.005)
* Uses C=256 for the penalty parameter for the error term
* The model was trained on 1,800 items in each of 48 file type classes
*  The features used for classification in this version are the concatenated bi-gram and unigram count vectors:
   * features 1-65536 are bigrams `\x0000-\x00FF` then `\xFF00-\xFFFF`
   * features 65537-65792 are unigrams\x00-\xFF.

This version was statistically trained to identify 48 of the following file/data types:
	
	TEXT = 1, /* file type description: Plain text file extensions: .text, .txt */
	CSV = 2, /* file type description: Delimited file extensions: .csv */
	LOG = 3, /* file type description: Log files file extensions: .log */
	HTML = 4, /* file type description: HTML file extensions: .html */
	XML = 5, /* file type description: xml file extensions: .xml */
	JSON = 7, /* file type description: JSON records file extensions: .json */
	JS = 8, /* file type description: JavaScript code file extensions: .js */
	JAVA = 9, /* file type description: Java Source Code file extensions: .java */
	CSS = 10, /* file type description: css file extensions: .css */
	B64 = 11, /* file type description: Base64 encoding file extensions: .b64 */
	A85 = 12, /* file type description: Base85 encoding file extensions: .a85 */
	B16 = 13, /* file type description: Hex encoding file extensions: .b16 */
	URL = 14, /* file type description: URL encoding file extensions: .urlencoded */
	RTF = 16, /* file type description: Rich Text File file extensions: .rtf */
	TBIRD = 17, /* file type description: Thunderbird Mail Files (data and index) file extensions: .msf and no extension */
	PST = 18, /* file type description: MS Outlook PST files file extensions: .pst */
	PNG = 19, /* file type description: Portable Network Graphic file extensions:  */
	GIF = 20, /* file type description: GIF file extensions: .gif */
	TIF = 21, /* file type description: Bi-tonal images file extensions: .tif, .tiff */
	JB2 = 22, /* file type description: JBIG2 file extensions: .jb2 */
	GZ = 23, /* file type description: ZLIB - DEFLATE compression file extensions: .gz, .gzip, .tgz, .z, .taz */
	ZIP = 24, /* file type description: ZLIB - DEFLATE compression file extensions: .zip */
	BZ2 = 27, /* file type description: BZ2 file extensions: .bz, .tbz, .bz2, bzip2, .tbz2 */
	PDF = 28, /* file type description: PDF file extensions: .pdf */
	DOCX = 29, /* file type description: MS-DOCX file extensions: .docx */
	XLSX = 30, /* file type description: MS-XLSX file extensions: .xlsx */
	PPTX = 31, /* file type description: MS-PPTX file extensions: .pptx */
	JPG = 32, /* file type description: JPG file extensions: .jpg, .jpeg */
	MP3 = 33, /* file type description: MP3 file extensions: .mp3 */
	M4A = 34, /* file type description: AAC file extensions: .m4a */
	MP4 = 35, /* file type description: H264 file extensions: .mp4 */
	AVI = 36, /* file type description: AVI file extensions: .avi */
	WMV = 37, /* file type description: WMV file extensions: .wmv */
	FLV = 38, /* file type description: FLV file extensions: .flv */
	WAV = 40, /* file type description: Windows Audio File file extensions: .wav */
	MOV = 42, /* file type description: Apple Quicktime Move file extensions: .mov */
	DOC = 43, /* file type description: MS-DOC file extensions: .doc */
	XLS = 44, /* file type description: MS-XLS file extensions: .xls */
	PPT = 45, /* file type description: MS-PPT file extensions: .ppt */
	FAT = 46, /* file type description: FS-FAT file extensions: .fat */
	NTFS = 47, /* file type description: FS-NTFS file extensions: .ntfs */
	EXT3 = 48, /* file type description: FS-EXT file extensions: .ext3 */
	EXE = 49, /* file type description: Windows Portable Executables (PE) file extensions: .exe */
	DLL = 50, /* file type description: Windows Dynamic Link Library files file extensions: .dll */
	ELF = 51, /* file type description: Linux Executables file extensions: .elf */
	BMP = 52, /* file type description: Bitmap file extensions: .bmp */
	
This version of Sceadan detects the following types by rule or pattern:
	
- Random 
- Constant

This version has support for enabling advanced feature extraction and usage in future versions and with use of other model files.

*NOTE:* The formulas contained in the code are not verified as correct and should be used only with extreme caution.

Such features include: 

- Bi-gram entropy
- Hamming weight
- Mean byte value
- Standard deviation of byte values
- Absolute deviation of byte values
- Skewness
- Kurtosis
- Compressed item length -- Burrows-Wheeler algorithm (bzip)
- Compressed item length -- Lempel-Ziv-Welch algorithm (LZW used by zlib and gzip)
- Average contiguity between bytes
- Max byte streak
- Low ASCII frequency
- Medium ASCII frequency
- High ASCII frequency
NOTE: Experimentation and testing showed the bi-gram/unigram byte count vectors achieved higher prediction accuracy than other combinations which included some or all of the above advanced features in a 38-class scenario.

Should advanced features be enabled, the following dependencies will be required:

- zlib v1.2.7
- libbz v1.0.2-5

Modes of operation:

- AVAILABLE: "Container mode": calculates statistics on the entire input item (file)
NOT AVAILABLE: "Block mode": calculates statistics on blocks of specified size within a container file; marches through container at specified byte-level offsets
- NOT AVAILABLE:"Feature vector extraction mode": does not predict type; simply produces vector files compatible with libsvm and liblinear format; useful for researchers who want to use Sceadan to generate vector files for research experiments 


Bugs and Platforms Tested On
-------

This version will build on Linux and Mac environments. 

This version calculates hamming weight and entropy values incorrectly.
This has no impact on the default operation, since Sceadan does not
use these features in this version's prediction model.


Where to find things
---
You'll want to be familiar with:
* http://www.csie.ntu.edu.tw/~cjlin/libsvm/ - libsvm - where we got a lot of the python training scripts from
* http://www.csie.ntu.edu.tw/~cjlin/liblinear/ - liblinear - the upgrade to libsvm that trains in linear time


Acknowledgments
----
Foundational research, design, and project management by Nicole L. Beebe, Ph.D. at The University of Texas at San Antonio (UTSA)

Software development by Laurence Maddox and Lishu Liu, UTSA.

Experimentation by Lishu Liu, UTSA.

Special thanks to DJ Bauch, SAIC, for software development assistance.

Special thanks to Minghe Sun, Ph.D., UTSA for support vector machine assistance.

Special thanks to Matt Beebe for tool naming inspiration.

This research was supported in part by a research grant from the
U.S. Naval Postgraduate School (Grant No. N00244-11-1-0011), and in
part by funding from the UTSA Provost Summer Research Mentorship
program.

