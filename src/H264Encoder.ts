import type { EncoderInputType, EncoderOption, RawH264Encoder, RawH264EncoderConstructor } from './types';

// eslint-disable-next-line @typescript-eslint/no-var-requires
const { H264Encoder: _H264Encoder } = require('../build/Release/h264.node') as {
  H264Encoder: RawH264EncoderConstructor;
};
class H264Encoder {
  encoder: RawH264Encoder;
  constructor(
    option: Omit<EncoderOption, 'feed_type' | 'pixel_format'> & {
      /** input frame data type, fd or buffer
       * @default fd
       */
      inputType?: EncoderInputType;
      /** pixel format fourcc */
      pixelFormat?: number;
    },
    callback?: (
      err: unknown,
      ok: boolean,
      data: {
        nalu: number;
        data: ArrayBuffer;
      },
    ) => void,
  ) {
    let _callback = callback;
    if (!_callback) {
      _callback = () => {};
    }
    const newOption = { ...option, pixel_format: option.pixelFormat, feed_type: option.inputType } as EncoderOption;
    this.encoder = new _H264Encoder(newOption, _callback);
  }
  feed(data: number | ArrayBuffer, size: number) {
    return this.encoder.feed(data, size);
  }

  stop() {
    return this.encoder.stop();
  }
}

export default H264Encoder;
