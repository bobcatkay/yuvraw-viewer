#pragma once

namespace FThirdPartyNotices
{
    /**
     * 随程序内置主要归属与许可声明。构建还会把 vcpkg 中各依赖的完整
     * copyright/license 文件复制到输出目录的 licenses 子目录。
     * 版本号与当前工程实际编译的依赖保持一致，升级依赖时需要同步更新。
     */
    inline constexpr const char* Text = R"NOTICE(YUVRaw Third-Party Software Notices

This application includes the following third-party software:

  - Dear ImGui 1.92.0 WIP
  - GLFW 3.4
  - OpenGL Mathematics (GLM) 1.0.3
  - glad 0.1.36 generated OpenGL loader
  - Khronos khrplatform.h
  - stb_rect_pack 1.01, stb_textedit 1.14 and stb_truetype 1.26
  - LibRaw 0.22.2 (used under LGPL 2.1)
  - Little CMS 2.19.1
  - zlib 1.3.2
  - libjpeg-turbo 3.2.0
  - JasPer 4.2.9
  - ProggyClean font
  - Noto Sans CJK SC Regular 2.004 (SIL Open Font License 1.1)

Complete package notices and license texts are distributed beside the
application in the "licenses" directory. YUVRaw's own code is licensed
under GPL version 3; see LICENSE. See SOURCE_DISTRIBUTION.md for the
matching source archive and instructions for modifying and relinking LibRaw.
The complete Chinese fallback font is distributed in resources/fonts with
its original OFL-1.1.txt and a source/version/hash record in README.md.

===============================================================================
DNG decoding dependencies
===============================================================================

LibRaw 0.22.2
Copyright (C) 2008-2025 LibRaw LLC

YUVRaw selects the GNU Lesser General Public License version 2.1
(SPDX: LGPL-2.1-only) from LibRaw's dual-license offer. See
licenses/LGPL-2.1.txt and licenses/LibRaw.txt for the license and attributions.
Internal DCB/FBDD, X3F and Adobe DNG SDK portions retain their respective
BSD-style/MIT terms; see licenses/LibRaw-components.txt for their full texts.
The upstream notices retain the alternative CDDL text; it is not the license
selected by YUVRaw. The upstream source is available at:
https://github.com/LibRaw/LibRaw/tree/0.22.2

Each release provides a matching source archive beside the binary ZIP.
It includes YUVRaw source, LibRaw 0.22.2 source, the pinned LibRaw-cmake
sources, the exact vcpkg recipes and patches, and the other library sources
needed to rebuild and relink the program. The vcpkg recipe modifies the
LibRaw CMake build files and the installed static-library header; these
changes are documented in SOURCE_DISTRIBUTION.md. This source delivery
implements the source/relinking option in LGPL 2.1 section 6(a), with
equivalent download access under section 6(d).

Little CMS 2.19.1
Copyright (c) 2023 Marti Maria Saguer
Licensed under the MIT License. See licenses/Little-CMS.txt.

zlib 1.3.2
Copyright (C) 1995-2022 Jean-loup Gailly and Mark Adler
Licensed under the zlib License. See licenses/zlib.txt.

libjpeg-turbo 3.2.0
This software is based in part on the work of the Independent JPEG Group.
See licenses/libjpeg-turbo.txt for the IJG and BSD-style license notices.

JasPer 4.2.9
Copyright (c) 2001-2016 Michael David Adams
Copyright (c) 1999-2000 Image Power, Inc.
Copyright (c) 1999-2000 The University of British Columbia
Licensed under the JasPer License Version 2.0. See licenses/JasPer.txt.

===============================================================================
Dear ImGui
===============================================================================

The MIT License (MIT)

Copyright (c) 2014-2025 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

===============================================================================
GLFW
===============================================================================

Copyright (c) 2002-2006 Marcus Geelnard
Copyright (c) 2006-2019 Camilla Löwy

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the
use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it freely,
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim
   that you wrote the original software. If you use this software in a product,
   an acknowledgment in the product documentation would be appreciated but is
   not required.

2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

3. This notice may not be removed or altered from any source distribution.

===============================================================================
OpenGL Mathematics (GLM)
===============================================================================

The MIT License

Copyright (c) 2005 - G-Truc Creation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

===============================================================================
glad generated loader
===============================================================================

The OpenGL loader was generated by glad 0.1.36. Code generated by glad is
offered under Public Domain, WTFPL or CC0 terms. The generated loader also
uses Khronos material covered by the notice below.

===============================================================================
Khronos khrplatform.h
===============================================================================

Copyright (c) 2008-2018 The Khronos Group Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and/or associated documentation files (the "Materials"), to
deal in the Materials without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Materials, and to permit persons to whom the Materials are
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Materials.

THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE MATERIALS OR THE USE OR OTHER DEALINGS IN THE
MATERIALS.

===============================================================================
stb libraries used by Dear ImGui
===============================================================================

The MIT License

Copyright (c) 2017 Sean Barrett

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

===============================================================================
ProggyClean font embedded in Dear ImGui
===============================================================================

The MIT License

Copyright (c) 2004, 2005 Tristan Grimmer

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
)NOTICE";
}
