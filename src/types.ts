export interface RawH264Encoder {
  feed: (data: number | ArrayBuffer, size: number) => number;
  stop: () => number;
}

export enum EncoderInputType {
  /** File Descriptor */
  FD = 1,
  /** YUV Buffer */
  BUFFER = 2,
}

export interface EncoderOption {
  width: number;
  height: number;
  level: number;
  bitrate: number;
  pixel_format: number;
  bytesperline: number;
  invokeCallback?: boolean;
  framerate: number;
  file?: string;
  feed_type: 1 | 2;
}

export interface RawH264EncoderConstructor {
  new (
    option: EncoderOption,
    callback?: (
      err: unknown,
      ok: boolean,
      data: {
        nalu: number;
        data: ArrayBuffer;
      },
    ) => void,
  ): RawH264Encoder;
}
