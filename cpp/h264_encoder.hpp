#ifndef _H264_ENCODER_H_
#define _H264_ENCODER_H_ 1
#include <condition_variable>
#include <fcntl.h>
#include <iostream>
#include <libcamera/libcamera.h>
#include <linux/videodev2.h>
#include <mutex>
#include <napi.h>
#include <optional>
#include <span>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "util.hpp"

using namespace Napi;

struct frame_data_t
{
    uint32_t size;
    uint8_t *data;
};

using FrameType = frame_data_t *;

struct buffer
{
    void *start;
    int length;
    struct v4l2_buffer inner;
    struct v4l2_plane plane;
};

// static int get_v4l2_colorspace(std::optional<libcamera::ColorSpace> const &cs)
// {
//     if (cs == libcamera::ColorSpace::Rec709)
//         return V4L2_COLORSPACE_REC709;
//     else if (cs == libcamera::ColorSpace::Smpte170m)
//         return V4L2_COLORSPACE_SMPTE170M;

//     // LOG(1, "H264: surprising colour space: " << libcamera::ColorSpace::toString(cs));
//     return V4L2_COLORSPACE_SMPTE170M;
// }

// mmaps the buffers for the given type of device (capture or output).
void map(int fd, uint32_t type, struct buffer *buffer, enum v4l2_memory mem_type)
{
    struct v4l2_buffer *inner = &buffer->inner;

    memset(inner, 0, sizeof(*inner));
    inner->type = type;
    inner->memory = mem_type;

    inner->index = 0;
    inner->length = 1;
    inner->m.planes = &buffer->plane;
    ioctl(fd, VIDIOC_QUERYBUF, inner);
    std::cout << "map plane length after query buffer: " << ((&(buffer->plane))[0]).length << std::endl;
    buffer->length = inner->m.planes[0].length;
    buffer->start = mmap(NULL, buffer->length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, inner->m.planes[0].m.mem_offset);
    if (buffer->start == (void *)-1)
    {
        // std::cout << "mmap type: " << type << std::endl;
        printf("mmap error : %d-%s. \r\n", errno, strerror(errno));
    }
}

class EncoderWorker : public AsyncProgressWorker<FrameType>
{
  public:
    uint32_t width = 640;
    uint32_t height = 480;
    uint32_t bitrate_bps = 4 * 1024 * 1024;
    int level = V4L2_MPEG_VIDEO_H264_LEVEL_4_2;
    uint32_t pixel_format = V4L2_PIX_FMT_YUYV;
    uint8_t num_planes = 1;
    struct buffer output;
    struct buffer capture;
    int fd = open("/dev/video11", O_RDWR);
    FILE *file = NULL;
    bool stopped = false;
    std::mutex mtx; // 互斥量，保护产品缓冲区
    std::condition_variable frame_available;

    bool invoke_callback = true;
    uint32_t total_frame = 0;
    uint32_t total_size = 0;
    long long feed_time = 0;
    uint32_t poll_num = 0;

    enum v4l2_memory output_mem_type = V4L2_MEMORY_DMABUF;

    /**
     * 1: feed fd;
     * 2: feed buffer;
     */
    uint8_t feed_type = 1;

    EncoderWorker(Napi::Object option, Napi::Function callback) : AsyncProgressWorker(callback)
    {
        if (option.Get("width").IsNumber())
            width = option.Get("width").As<Napi::Number>().Uint32Value();
        if (option.Get("height").IsNumber())
            height = option.Get("height").As<Napi::Number>().Uint32Value();
        if (option.Get("bitrate").IsNumber())
            bitrate_bps = option.Get("bitrate").As<Napi::Number>().Uint32Value();
        if (option.Get("level").IsNumber())
            level = option.Get("level").As<Napi::Number>().Uint32Value();
        if (option.Get("pixel_format").IsNumber())
            pixel_format = option.Get("pixel_format").As<Napi::Number>().Uint32Value();
        if (option.Get("num_planes").IsNumber())
            num_planes = option.Get("num_planes").As<Napi::Number>().Uint32Value();
        if (option.Get("invokeCallback").IsBoolean())
            invoke_callback = option.Get("invokeCallback").As<Napi::Boolean>();
        if (option.Get("file").IsString())
            file = fopen(option.Get("file").As<Napi::String>().Utf8Value().c_str(), "w");
        if (option.Get("feed_type").IsNumber())
        {
            auto _feed_type = option.Get("feed_type").As<Napi::Number>().Uint32Value();
            if (_feed_type == 1 || _feed_type == 2)
            {
                feed_type = _feed_type;
            }
            if (feed_type == 1)
            {
                output_mem_type = V4L2_MEMORY_DMABUF;
            }
            if (feed_type == 2)
            {
                output_mem_type = V4L2_MEMORY_MMAP;
            }
        }

        if (option.Get("controllers").IsArray())
        {
            Napi::Array controllers = option.Get("controllers").As<Napi::Array>();
            for (int i = 0; i < controllers.Length(); i++)
            {
                Napi::Object ctrl_obj = controllers.Get(i).As<Napi::Object>();
                uint32_t id = ctrl_obj.Get('id').As<Napi::Number>().Uint32Value();
                int32_t value = ctrl_obj.Get('value').As<Napi::Number>().Int32Value();
                v4l2_control ctrl = {};
                ctrl.id = id;
                ctrl.value = value;
                ioctl(fd, VIDIOC_S_CTRL, &ctrl);
            }
        }
        // 设置码率
        v4l2_control ctrl = {};
        ctrl.id = V4L2_CID_MPEG_VIDEO_BITRATE;
        ctrl.value = bitrate_bps;
        ioctl(fd, VIDIOC_S_CTRL, &ctrl);
        ctrl.id = V4L2_CID_MPEG_VIDEO_H264_LEVEL;
        ctrl.value = level;
        ioctl(fd, VIDIOC_S_CTRL, &ctrl);

        struct v4l2_format fmt;
        fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        ioctl(fd, VIDIOC_G_FMT, &fmt);
        fmt.fmt.pix_mp.width = width;
        fmt.fmt.pix_mp.height = height;
        fmt.fmt.pix_mp.pixelformat = pixel_format;
        fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
        fmt.fmt.pix_mp.num_planes = num_planes;
        if (option.Get("bytesperline").IsNumber())
            fmt.fmt.pix_mp.plane_fmt[0].bytesperline = option.Get("bytesperline").As<Napi::Number>().Uint32Value();
        if (option.Get("colorspace").IsNumber())
            fmt.fmt.pix_mp.colorspace = option.Get("colorspace").As<Napi::Number>().Uint32Value();
        ioctl(fd, VIDIOC_S_FMT, &fmt);

        fmt = {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(fd, VIDIOC_G_FMT, &fmt);
        fmt.fmt.pix_mp.width = width;
        fmt.fmt.pix_mp.height = height;
        fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
        fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
        fmt.fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
        fmt.fmt.pix_mp.num_planes = 1;
        fmt.fmt.pix_mp.plane_fmt[0].bytesperline = 0;
        fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 1024 << 10;
        ioctl(fd, VIDIOC_S_FMT, &fmt);

        if (option.Get("framerate").IsNumber())
        {
            auto framerate = option.Get("framerate").As<Napi::Number>().Uint32Value();
            struct v4l2_streamparm params;
            memset(&params, 0, sizeof(params));
            params.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            params.parm.output.timeperframe.numerator = 1;
            params.parm.output.timeperframe.denominator = framerate;
            ioctl(fd, VIDIOC_S_PARM, &params);
        }

        struct v4l2_requestbuffers buf;
        buf.count = 1;
        // struct buffer output;
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buf.memory = output_mem_type;
        ioctl(fd, VIDIOC_REQBUFS, &buf);
        if (feed_type == 2)
        {
            map(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, &output, output_mem_type);
        }

        // struct buffer capture;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        ioctl(fd, VIDIOC_REQBUFS, &buf);
        map(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, &capture, V4L2_MEMORY_MMAP);

        ioctl(fd, VIDIOC_QBUF, capture.inner);

        int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        ioctl(fd, VIDIOC_STREAMON, &type);

        type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(fd, VIDIOC_STREAMON, &type);
    }

    void Execute(const ExecutionProgress &progress)
    {
        while (true)
        {
            if (stopped)
            {
                break;
            }
            // usleep(8 * 1000);

            pollfd p = {fd, POLLIN, 0};
            int ret = poll(&p, 1, 200);
            poll_num++;
            if (ret == -1)
            {
                std::cerr << std::strerror(errno) << std::endl;
                if (errno == EINTR)
                    continue;
                throw std::runtime_error("unexpected errno " + std::to_string(errno) + " from poll");
            }
            // std::cout << "poll result: " << ret << std::endl;
            if (p.revents & POLLIN)
            {
                struct v4l2_buffer buf;
                struct v4l2_plane out_planes;
                buf.memory = output_mem_type;
                buf.length = 1;
                memset(&out_planes, 0, sizeof(out_planes));
                buf.m.planes = &out_planes;
                // 将output buffer出列
                buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                ioctl(fd, VIDIOC_DQBUF, &buf);
                // 将capture buffer出列
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.length = 1;
                memset(&out_planes, 0, sizeof(out_planes));
                buf.m.planes = &out_planes;
                ioctl(fd, VIDIOC_DQBUF, &buf);
                // 提取capture buffer里的编码数据，即H264数据
                int encoded_len = buf.m.planes[0].bytesused;
                if (encoded_len > 0)
                {
                    long long current = millis();
                    // std::cout << fd << "--encoded frame: " << total_frame << " cost: " << current - feed_time << ", size: " << encoded_len / 1024.0 << std::endl;
                    total_frame++;
                    total_size += encoded_len;
                    if (file != NULL)
                    {
                        size_t ret = fwrite(capture.start, sizeof(uint8_t), encoded_len, file);
                        if (ret < 0)
                        {
                            printf("write file error: %s \n", strerror(errno));
                        }
                    }
                    if (invoke_callback && !Callback().IsEmpty())
                    {
                        FrameType frame_data = new frame_data_t{encoded_len, (uint8_t *)capture.start};
                        progress.Send(&frame_data, sizeof(frame_data_t));
                    }
                    else
                    {
                        // std::cout << "encoded size: " << encoded_len << std::endl;
                    }
                }
                // 将capture buffer入列
                if (ioctl(fd, VIDIOC_QBUF, capture.inner) < 0)
                {
                    throw std::runtime_error("failed to re-queue encoded buffer");
                }
            }
        }
    }

    void feed(uint8_t *plane_data, uint32_t size)
    {
        // uint32_t size = data.Get("size").As<Napi::Number>().Uint32Value();
        // uint8_t *plane_data = (uint8_t *)data.Get("data").As<Napi::ArrayBuffer>().Data();
        memcpy(output.start, plane_data, size);
        ioctl(fd, VIDIOC_QBUF, output.inner);
    }

    void feed(int _fd, uint32_t size)
    {
        if (stopped)
            return;
        v4l2_buffer buf = {};
        v4l2_plane planes[VIDEO_MAX_PLANES] = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buf.index = 0;
        buf.field = V4L2_FIELD_NONE;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.length = 1;
        buf.m.planes = planes;
        buf.m.planes[0].m.fd = _fd;
        buf.m.planes[0].bytesused = size;
        buf.m.planes[0].length = size;
        ioctl(fd, VIDIOC_QBUF, &buf);
        feed_time = millis();
        // std::cout << fd << "--feed frame: " << total_frame << " at: " << feed_time << std::endl;
    }

    int setController(Napi::Object ctrl_obj)
    {
        auto id = ctrl_obj.Get("id").As<Napi::Number>().Uint32Value();
        auto value = ctrl_obj.Get("value").As<Napi::Number>().Int32Value();
        auto code = ctrl_obj.Get("code").As<Napi::Number>().Uint32Value();
        v4l2_control ctrl = {};
        ctrl.id = id;
        ctrl.value = value;
        return ioctl(fd, code, &ctrl);
    }

    void stop()
    {

        // usleep(2000 * 1000);
        int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        ioctl(fd, VIDIOC_STREAMOFF, &type);
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(fd, VIDIOC_STREAMOFF, &type);
        struct v4l2_requestbuffers buf;
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buf.memory = output_mem_type;
        ioctl(fd, VIDIOC_REQBUFS, &buf);
        buf.memory = V4L2_MEMORY_MMAP;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(fd, VIDIOC_REQBUFS, &buf);
        stopped = true;
        if (file)
        {
            fclose(file);
        }
        if (1)
        {
            std::cout << "total frame: " << total_frame << ", total size: " << total_size / 1024.0 / 1024.0 << ", poll num: " << poll_num << std::endl;
        }
    }
    void OnError(const Error &e)
    {
        HandleScope scope(Env());
        Callback().Call({String::New(Env(), e.Message())});
    }
    void OnOK()
    {
        HandleScope scope(Env());
        Callback().Call({Env().Null(), String::New(Env(), "Ok")});
    }
    void OnProgress(const FrameType *data, size_t t)
    {
        if (Callback().IsEmpty() || !invoke_callback)
        {
            return;
        }
        HandleScope scope(Env());
        uint8_t *buf = (*data)->data;
        uint32_t size = (*data)->size;
        uint32_t start_post = 0;
        uint32_t len = size;
        int nal_type = -1;
        uint32_t last_pos = 0;
        if (size > 4)
        {
            uint8_t prefix[4] = {0x00, 0x00, 0x00, 0x01};
            uint32_t k = 0;
            std::vector<uint8_t *> pos_vec;
            for (;;)
            {
                uint8_t *pos = (uint8_t *)memmem(buf + k, size - k, prefix, 4);
                if (pos == NULL)
                    break;
                pos_vec.push_back((uint8_t *)(pos));
                k = pos - buf + 4;
            }
            for (int i = 0; i < pos_vec.size(); i++)
            {
                size_t len = i == pos_vec.size() - 1 ? size - (pos_vec[i] - buf) : pos_vec[i + 1] - pos_vec[i];
                Napi::ArrayBuffer buffer = Napi::ArrayBuffer::New(Env(), pos_vec[i], len, [](Napi::Env env, void *arg) {});
                Napi::Object payload = Napi::Object::New(Env());
                nal_type = (int)(pos_vec[i][4]) & 0x1f;
                payload.Set("nalu", nal_type);
                payload.Set("data", buffer);
                Callback().Call({Env().Null(), Env().Null(), payload});
            }

            // uint32_t i = 0;
            // for (;;)
            // {
            //     int current_type = -1;
            //     uint8_t skip_len = 1;
            //     if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 0 && buf[i + 3] == 1)
            //     {
            //         current_type = (int)(buf[i + 4]) & 0x1f;
            //         skip_len = 4;
            //     }
            //     if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1)
            //     {
            //         current_type = (int)(buf[i + 3] & 0x1f);
            //         skip_len = 3;
            //     }
            //     if (current_type < 0 && buf[i + 1] != 0)
            //     {
            //         skip_len++;
            //     }
            //     if (current_type < 0 && buf[i + 2] != 0)
            //     {
            //         skip_len++;
            //     }
            //     // find a new nalu or at end
            //     if (current_type >= 0 || (size - i) <= 4)
            //     {
            //         // end previous nalu data or last nalu data
            //         if (nal_type >= 0)
            //         {
            //             auto len = (size - i) <= 4 ? size - last_pos : i - last_pos;
            //             uint8_t *seg_buf = new uint8_t[len];
            //             memcpy(seg_buf, buf + last_pos, len);
            //             Napi::ArrayBuffer buffer = Napi::ArrayBuffer::New(Env(), seg_buf, len, [](Napi::Env env, void *arg) { delete[] arg; });
            //             Napi::Object payload = Napi::Object::New(Env());
            //             payload.Set("nalu", nal_type);
            //             payload.Set("data", buffer);
            //             Callback().Call({Env().Null(), Env().Null(), payload});
            //         }
            //         // find new nalu
            //         if (current_type >= 0)
            //         {
            //             nal_type = current_type;
            //             last_pos = i;
            //         }
            //         else
            //         {
            //             // at end
            //             break;
            //         }
            //     }
            //     i += skip_len;
            // }
        }
    }
};

class H264Encoder : public Napi::ObjectWrap<H264Encoder>
{
  public:
    static Napi::FunctionReference *constructor;
    EncoderWorker *worker;
    H264Encoder(const Napi::CallbackInfo &info) : Napi::ObjectWrap<H264Encoder>(info)
    {
        Napi::Object option = info[0].As<Napi::Object>();
        Napi::Function callback = info[1].As<Napi::Function>();
        Napi::HandleScope scope(info.Env());
        worker = new EncoderWorker(option, callback);
        worker->Queue();
    }
    Napi::Value feed(const Napi::CallbackInfo &info)
    {
        Napi::Value param = info[0].As<Napi::Value>();
        if (param.IsArrayBuffer())
        {
            uint8_t *plane_data = (uint8_t *)param.As<Napi::ArrayBuffer>().Data();
            worker->feed(plane_data, info[1].As<Napi::Number>().Uint32Value());
        }
        else if (param.IsNumber())
        {
            worker->feed(param.As<Napi::Number>().Int32Value(), info[1].As<Napi::Number>().Uint32Value());
        }

        return Napi::Number::New(info.Env(), 0);
    }

    Napi::Value stop(const Napi::CallbackInfo &info)
    {
        worker->stop();
        return Napi::Number::New(info.Env(), 0);
    }

    Napi::Value setController(const Napi::CallbackInfo &info)
    {
        Napi::Object data = info[0].As<Napi::Object>();
        Napi::HandleScope scope(info.Env());
        int ret = worker->setController(data);
        return Napi::Number::New(info.Env(), Napi::Number::New(info.Env(), ret));
    }
    static Napi::Object Init(Napi::Env env, Napi::Object exports)
    {
        Napi::Function func = DefineClass(env, "H264Encoder",
                                          {
                                              InstanceMethod<&H264Encoder::feed>("feed", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
                                              InstanceMethod<&H264Encoder::stop>("stop", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
                                              InstanceMethod<&H264Encoder::setController>("setController", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),

                                          });
        *constructor = Napi::Persistent(func);
        exports.Set("H264Encoder", func);
        return exports;
    }
};
Napi::FunctionReference *H264Encoder::constructor = new Napi::FunctionReference();
#endif