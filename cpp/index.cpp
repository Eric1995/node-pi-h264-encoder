#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <linux/dma-buf.h>
#include <map>
#include <memory>
#include <napi.h>
#include <thread>
#include <unistd.h>

#include "h264_encoder.hpp"

// Initialize native add-on
Napi::Object Init(Napi::Env env, Napi::Object exports)
{

    H264Encoder::Init(env, exports);

    return exports;
}

NODE_API_MODULE(addon, Init)