{
  "targets": [
    {
      "target_name": "glif_native",
      "sources": [
        "src/addon.cc",
        "../../src/image.c",
        "../../src/sampling.c",
        "../../src/grid.c",
        "../../src/font.c",
        "../../src/contrast.c",
        "../../src/match.c",
        "../../src/output.c",
        "../../src/temporal.c"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "../../vendor",
        "../../src"
      ],
      "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"],
      "cflags_c": ["-std=c11", "-O2"],
      "cflags_cc": ["-std=c++17", "-O2"],
      "conditions": [
        ["OS=='mac'", {
          "cflags_c": [
            "-Xpreprocessor", "-fopenmp",
            "-I<!(brew --prefix libomp 2>/dev/null)/include"
          ],
          "libraries": [
            "-L<!(brew --prefix libomp 2>/dev/null)/lib",
            "-lomp"
          ],
          "xcode_settings": {
            "GCC_C_LANGUAGE_STANDARD": "c11",
            "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
            "OTHER_CFLAGS": [
              "-O2", "-Xpreprocessor", "-fopenmp",
              "-I<!(brew --prefix libomp 2>/dev/null)/include"
            ],
            "OTHER_CPLUSPLUSFLAGS": ["-O2"],
            "OTHER_LDFLAGS": [
              "-L<!(brew --prefix libomp 2>/dev/null)/lib",
              "-lomp"
            ]
          }
        }],
        ["OS=='linux'", {
          "sources": [
            "../../src/platform/linux/v4l2_output.c"
          ],
          "cflags_c": ["-fopenmp"],
          "cflags_cc": ["-fopenmp"],
          "libraries": ["-lgomp"],
          "defines": ["_DEFAULT_SOURCE"]
        }]
      ]
    }
  ]
}
