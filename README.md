# IVTC DN VapourSynth Plugin

This is a small plugin to perform Inverse Telecine (IVTC) of a [VapourSynth](https://github.com/vapoursynth/vapoursynth) clip based on an [IVTC DN](https://github.com/Mikewando/IVTC-DN) project file.

It is intended only for situations where automatic field matching produces undesirable results. For example if a source has production issues that have degraded individual fields.

Only constant rate field-based material is supported. Furthermore only one cycle is supported; 10 input fields will always yield 4 output frames.

## Usage

### Plugin Documentation

```
.. function:: IVTC(clip clip, string projectfile[, bint rawproject=False, clip linedoubled])
   :module: ivtcdn

   Will IVTC the given clip based on the actions described in the projectfile.

   Parameters:
      clip
         Input clip. Fields should already be separated (e.g. with `std.SeparateFields()`).

      projectfile
         The path to the IVTC DN project file.

      rawproject
         The projectfile arugment contains decompressed json contents of a project file rather than a path to the project file. This is only really intended for internal use by the IVTC DN GUI tool. 

      linedoubled
         If a single field is selected for an output frame, the selected field will be naively doubled to match the height of other output frames. If more sophisticated line doubling is desirable, this option can be used to provide a clip which will be used to substitute such frames instead.
```

### Example

```python
import vapousynth as vs
import havsfunc as haf

clip = vs.core.d2v.Source('example.d2v')
qtgmc = haf(clip, preset='Fast')
clip = clip.std.SeparateFields()
clip = clip.ivtcdn.IVTC('project.ivtc', linedoubled=qtgmc)
clip.set_output()
```

### Project File Discussion

This plugin is designed to be paired with the IVTC DN GUI tool, but it is certainly _possible_ to use it independently if a project file with matching structure is used. The structure is not overly complicated so while this may not be a complete specification, it is hopefully sufficient for most purposes.

Broadly the project file is just a zlib compressed json file. The structure of the json object is currently:
```js
{
    "ivtc_actions": [0, ...], // An array of integers with one entry for every input field (see later definition)
    "notes": ["A", ...], // An array of strings with one entry for every input field (ignored by plugin, used only by GUI)
    "no_match_actions": { // A map of output_frame => action for output frames which should use non-default action if no input fields are matched (default is "[Use ]Previous[ Frame]")
        1234: "Next"
    },
    "scene_changes" [123], // An array of integers with each entry representing the input field which starts a new scene (ignored by plugin, used only by GUI)
    "project_garbage": { // (ignored by plugin, used only by GUI)
        "active_cycle": 0,
        "script_file": "/home/users/example/example.vpy"
    }
}
```

An example script to illustrate inspecting a project file:
```py
import json
import zlib

with open('example.ivtc', 'rb') as project_file:
    compressed_contents = project_file.read()

decompressed_contents = zlib.decompress(compressed_contents)
project = json.loads(decompressed_contents)
print(json.dumps(project['project_garbage'], indent=2))
```

The valid values for `ivtc_actions` are:
```py
{
    # Normal matching
    # If there are duplicates in a cycle the first occurrence will be used
    0: 'Top Frame 0',
    1: 'Bottom Frame 0',
    2: 'Top Frame 1',
    3: 'Bottom Frame 1',
    4: 'Top Frame 2',
    5: 'Bottom Frame 2',
    6: 'Top Frame 3',
    7: 'Bottom Frame 3',
    
    # Field will be dropped
    8: 'Drop',
    
    # Allows first field of N+1th cycle to complete Nth cycle
    # Necessary for sources which are edited after telecining
    9: 'Complete Previous Cycle',
}
```

## Building

The author isn't overly familiar with C++ "build systems" so details may be missing. Broadly the only dependency not included with the source should be the VapourSynth SDK. The build system tested is MSYS2's mingw-w64 on Windows 10. It is quite likely that building is possible on other systems given sufficent experience building C++ libraries using those other systems.

```sh
meson setup -Dstatic=true build
cd build/
ninja

# If all goes well libivtcdn.dll should exist within the build directory
```

## Bundled Dependencies

Some dependencies are directly copied into the `src/` directory from their respective projects. It is the author's understanding that this usage is compatible with the applicable licenses.

From [libp2p](https://github.com/sekrit-twc/libp2p/tree/ed0a37adf0fdab2af95845fc80e31a6b59debebe) fetched 2022-04-30
 - `p2p.h`
 - `p2p_api.h`
 - `p2p_api.cpp`
 - `v210.cpp`

From [nlohmann/json](https://github.com/nlohmann/json/tree/a8a547d7a212a6a39943bbd5b4220be504a1a33e) fetched 2022-05-15
 - `json.hpp`

From [miniz](https://github.com/richgel999/miniz/tree/76b3a872855388c735c564905da030f26334f3b3) fetched 2022-05-16
 - `miniz.h`
 - `zip.h`
 - `zip.c`

 From [gzip-hpp](https://github.com/mapbox/gzip-hpp/tree/674359bcbe87389bb947f90339c7e4250457745e) fetched 2022-05-16 with modifications
 - `gzip/decompress.hpp`