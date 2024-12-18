{
    "targets": [
        {
            "target_name": "h264",
            "sources": ["cpp/index.cpp"],
            "include_dirs": [
                "<!@(node -p \"require('node-addon-api').include\")",
                "/usr/local/include/libcamera",
                "/usr/include/libcamera",
                "/home/eric/Dev/node-libcamera/include",
                "/usr/aarch64-linux-gnu/include",
            ],
            "dependencies": [
                "<!@(node -p \"require('node-addon-api').gyp\")"
            ],
            "libraries": [],
            "cflags": [
                "-std=c++20",
                "-fpermissive",
                "-fexceptions",
                "-g",
                "-O0",
            ],  
            "cflags_cc": [
                "-std=c++20",
                "-fpermissive",
                "-fexceptions",
                "-g",
                "-O0",
            ],  
            'defines': ["NAPI_CPP_EXCEPTIONS", "PI"],
        },
    ]
}
