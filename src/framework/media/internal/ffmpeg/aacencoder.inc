/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: AAC-LC encoding with FFmpeg's own ("native") AAC encoder, which is part of FFmpeg's LGPL code
// (decision D-E15; it replaces the Fraunhofer FDK AAC library). Writes an ADTS stream.
//
// This file is included by each ffmpeg/vN/videoencoder.cpp, inside its namespace muse::media::ffmpeg::vN and
// after that version's FFmpeg headers, so one implementation serves every FFmpeg version Jubelle can load.
// Only functions the library handler already loads are used (plus avcodec_find_encoder_by_name).

class AacEncoder : public IAacEncoder
{
public:
    explicit AacEncoder(std::shared_ptr<FFmpegLibHandler> handler)
        : m_ff(std::move(handler)) {}
    ~AacEncoder() override { close(); }

    AacEncoder(const AacEncoder&) = delete;
    AacEncoder& operator=(const AacEncoder&) = delete;

    bool open(const io::path_t& path, int sampleRate, int channels, int bitRateKbps) override
    {
        close();
        m_error.clear();
        if (!m_ff) {
            return fail("the FFmpeg libraries aren't loaded");
        }
        if (channels < 1 || channels > 2) {
            return fail("only mono and stereo AAC are supported");
        }
        if (sampleRate <= 0 || bitRateKbps <= 0) {
            return fail("invalid sample rate or bit rate");
        }

        // By name, so that FFmpeg's own encoder is used even when a build also has others (AudioToolbox,
        // Media Foundation). It produces AAC-LC by default.
        const AVCodec* codec = m_ff->avcodec_find_encoder_by_name("aac");
        if (!codec) {
            return fail("these FFmpeg libraries have no AAC encoder");
        }
        m_codecName = codec->name ? codec->name : "aac";

        m_ctx = m_ff->avcodec_alloc_context3(codec);
        if (!m_ctx) {
            return fail("out of memory");
        }
        m_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
        m_ctx->sample_rate = sampleRate;
        m_ctx->bit_rate = int64_t(bitRateKbps) * 1000;
        m_ctx->time_base = AVRational { 1, sampleRate };
        setLayout(m_ctx, channels);

        if (m_ff->avformat_alloc_output_context2(&m_fmt, nullptr, "adts", path.c_str()) < 0 || !m_fmt) {
            m_fmt = nullptr;
            return fail("FFmpeg can't write ADTS (.aac) files");
        }
        if (m_fmt->oformat->flags & AVFMT_GLOBALHEADER) {
            m_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        const int opened = m_ff->avcodec_open2(m_ctx, codec, nullptr);
        if (opened < 0) {
            return fail("the AAC encoder couldn't start (error " + std::to_string(opened) + ")");
        }

        m_stream = m_ff->avformat_new_stream(m_fmt, nullptr);
        if (!m_stream || m_ff->avcodec_parameters_from_context(m_stream->codecpar, m_ctx) < 0) {
            return fail("couldn't set up the audio stream");
        }
        m_stream->time_base = m_ctx->time_base;

        if (!(m_fmt->oformat->flags & AVFMT_NOFILE)) {
            if (m_ff->avio_open(&m_fmt->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) {
                return fail("couldn't create the file");
            }
            m_ioOpen = true;
        }
        if (m_ff->avformat_write_header(m_fmt, nullptr) < 0) {
            return fail("couldn't write the start of the file");
        }

        m_frame = m_ff->av_frame_alloc();
        m_packet = m_ff->av_packet_alloc();
        if (!m_frame || !m_packet) {
            return fail("out of memory");
        }

        m_channels = channels;
        m_sampleRate = sampleRate;
        m_frameSize = m_ctx->frame_size > 0 ? m_ctx->frame_size : 1024;
        m_delay = m_ctx->initial_padding > 0 ? m_ctx->initial_padding : 0;
        m_planes.assign(size_t(channels), std::vector<float>(size_t(m_frameSize), 0.0f));
        m_fill = 0;
        m_pts = 0;
        m_open = true;
        return true;
    }

    int encoderDelay() const override { return m_delay; }

    bool encode(const float* interleaved, size_t frames) override
    {
        if (!m_open) {
            return fail("the encoder isn't open");
        }
        size_t i = 0;
        while (i < frames) {
            const size_t take = std::min(frames - i, size_t(m_frameSize - m_fill));
            for (size_t f = 0; f < take; ++f) {
                for (int c = 0; c < m_channels; ++c) {
                    m_planes[size_t(c)][size_t(m_fill) + f] = interleaved[(i + f) * size_t(m_channels) + size_t(c)];
                }
            }
            m_fill += int(take);
            i += take;
            if (m_fill == m_frameSize && !sendFrame(m_fill)) {
                return false;
            }
        }
        return true;
    }

    bool finish() override
    {
        if (!m_open) {
            return fail("the encoder isn't open");
        }
        bool ok = true;
        if (m_fill > 0) {
            ok = sendFrame(m_fill);   // the last frame may be shorter; FFmpeg pads it
        }
        if (ok) {
            ok = m_ff->avcodec_send_frame(m_ctx, nullptr) >= 0 && drain();
        }
        if (ok && m_ff->av_write_trailer(m_fmt) < 0) {
            ok = fail("couldn't write the end of the file");
        }
        close();
        return ok;
    }

    std::string encoderName() const override { return "FFmpeg " + m_codecName + " (LGPL)"; }
    std::string lastError() const override { return m_error; }

private:
    static void setLayout(AVCodecContext* ctx, int channels)
    {
#if LIBAVUTIL_VERSION_MAJOR >= 58
        ctx->ch_layout.order = AV_CHANNEL_ORDER_NATIVE;
        ctx->ch_layout.nb_channels = channels;
        ctx->ch_layout.u.mask = channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
#else
        ctx->channels = channels;
        ctx->channel_layout = channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
#endif
    }

    bool sendFrame(int samples)
    {
        m_frame->nb_samples = samples;
        m_frame->format = AV_SAMPLE_FMT_FLTP;
        m_frame->sample_rate = m_sampleRate;
#if LIBAVUTIL_VERSION_MAJOR >= 58
        m_frame->ch_layout = m_ctx->ch_layout;   // a native-order layout holds no allocated data
#else
        m_frame->channels = m_ctx->channels;
        m_frame->channel_layout = m_ctx->channel_layout;
#endif
        for (int c = 0; c < m_channels; ++c) {
            m_frame->data[c] = reinterpret_cast<uint8_t*>(m_planes[size_t(c)].data());
        }
        m_frame->linesize[0] = int(size_t(samples) * sizeof(float));
        m_frame->extended_data = m_frame->data;
        m_frame->pts = m_pts;
        m_pts += samples;
        m_fill = 0;

        // The frame isn't reference-counted, so FFmpeg copies the samples before returning
        const int sent = m_ff->avcodec_send_frame(m_ctx, m_frame);
        for (int c = 0; c < m_channels; ++c) {
            m_frame->data[c] = nullptr;
        }
        if (sent < 0) {
            return fail("AAC encoding failed (error " + std::to_string(sent) + ")");
        }
        return drain();
    }

    bool drain()
    {
        while (true) {
            const int got = m_ff->avcodec_receive_packet(m_ctx, m_packet);
            if (got == AVERROR(EAGAIN) || got == AVERROR_EOF) {
                return true;
            }
            if (got < 0) {
                return fail("AAC encoding failed (error " + std::to_string(got) + ")");
            }
            m_packet->stream_index = m_stream->index;
            m_ff->av_packet_rescale_ts(m_packet, m_ctx->time_base, m_stream->time_base);
            if (m_ff->av_interleaved_write_frame(m_fmt, m_packet) < 0) {
                m_ff->av_packet_unref(m_packet);
                return fail("couldn't write to the file");
            }
        }
    }

    void close()
    {
        if (m_frame) {
            m_ff->av_frame_free(&m_frame);
        }
        if (m_packet) {
            m_ff->av_packet_free(&m_packet);
        }
        if (m_ctx) {
            m_ff->avcodec_free_context(&m_ctx);
        }
        if (m_fmt) {
            if (m_ioOpen && m_fmt->pb) {
                m_ff->avio_close(m_fmt->pb);
                m_fmt->pb = nullptr;
            }
            m_ff->avformat_free_context(m_fmt);
            m_fmt = nullptr;
        }
        m_stream = nullptr;
        m_ioOpen = false;
        m_open = false;
    }

    bool fail(const std::string& message)
    {
        m_error = message;
        LOGE() << "AAC export: " << message;
        return false;
    }

    std::shared_ptr<FFmpegLibHandler> m_ff;
    AVCodecContext* m_ctx = nullptr;
    AVFormatContext* m_fmt = nullptr;
    AVStream* m_stream = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_packet = nullptr;
    std::vector<std::vector<float> > m_planes;
    std::string m_codecName;
    std::string m_error;
    int64_t m_pts = 0;
    int m_channels = 0;
    int m_sampleRate = 0;
    int m_frameSize = 1024;
    int m_fill = 0;
    int m_delay = 0;
    bool m_ioOpen = false;
    bool m_open = false;
};
