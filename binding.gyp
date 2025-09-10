{
    "variables" : {
        # use cross platform compile or not
        "FLAG": "<!(echo $FLAG)",
        "TARGET_ARCH": "<!(echo $TARGET_ARCH)",
    },
    "targets": [
        {
            "target_name": "h264_encoder",
            "sources": ["cpp/index.cpp"],
            "include_dirs": [
                "<!@(node -p \"require('node-addon-api').include\")"
            ],
            "dependencies": [
                "<!@(node -p \"require('node-addon-api').gyp\")"
            ],
            "libraries": [], 
            "cflags_cc": [
                "-std=c++23",
                "-fpermissive",
                "-fexceptions",
                "-g",
                "-O0",
            ],  
            "defines": ["NAPI_CPP_EXCEPTIONS", "PI"],
            "conditions": [
                ["FLAG=='CROSS'", {
                    "ldflags": [ "-nolibc", "-static-libstdc++", "-static-libgcc" ],
                    "conditions": [
                        ["TARGET_ARCH=='arm64' or TARGET_ARCH=='aarch64'", {
                            "cflags_cc": ["-target", "aarch64-linux-gnu"],  
                            "ldflags": ["-target", "aarch64-linux-gnu"]  
                        }],
                        ["TARGET_ARCH=='arm'", {
                            "cflags_cc": ["-target", "arm-linux-gnueabi"],        
                            "ldflags": ["-target", "arm-linux-gnueabi"]                      
                        }],
                    ],
                }]
            ]
        },
    ]
}
