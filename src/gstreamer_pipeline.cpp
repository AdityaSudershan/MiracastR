#include "gstreamer_pipeline.h"

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include <atomic>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>


struct PipelineElements
{
    GstElement* pipeline = nullptr;
    GstElement* appsrc = nullptr;
    GstElement* demux = nullptr;

    // Video
    GstElement* h264parse = nullptr;
    GstElement* videoqueue = nullptr;
    GstElement* videodecoder = nullptr;
    GstElement* videosinkqueue = nullptr;
    GstElement* videosink = nullptr;

    // Audio
    GstElement* aacparse = nullptr;
    GstElement* audioqueue = nullptr;
    GstElement* audiodecoder = nullptr;
    GstElement* audioconvert = nullptr;
    GstElement* audioresample = nullptr;
    GstElement* audiocaps = nullptr;
    GstElement* audiosink = nullptr;

    guint videoDemuxBuffers = 0;
    guint videoParseBuffers = 0;
    guint videoDecodedBuffers = 0;
    guint videoSinkBuffers = 0;

    guint audioDemuxBuffers = 0;
    guint audioParseBuffers = 0;
    guint audioDecodedBuffers = 0;
    guint audioSinkBuffers = 0;

    std::atomic<bool> started{false};
};


namespace
{

// ============================================================
// TIME FORMAT
// ============================================================

std::string timeString(GstClockTime t)
{
    if (!GST_CLOCK_TIME_IS_VALID(t))
        return "NONE";

    guint64 ms = GST_TIME_AS_MSECONDS(t);

    guint64 sec = ms / 1000;
    ms %= 1000;

    std::ostringstream out;

    out << sec << ".";

    if (ms < 100)
        out << "0";

    if (ms < 10)
        out << "0";

    out << ms << "s";

    return out.str();
}


// ============================================================
// VIDEO DEMUX PROBE
// ============================================================

static GstPadProbeReturn videoDemuxProbe(
    GstPad*,
    GstPadProbeInfo* info,
    gpointer userData)
{
    auto* e =
        static_cast<PipelineElements*>(userData);

    GstBuffer* buffer =
        GST_PAD_PROBE_INFO_BUFFER(info);

    if (!buffer)
        return GST_PAD_PROBE_OK;

    ++e->videoDemuxBuffers;

    if (e->videoDemuxBuffers == 1 ||
        e->videoDemuxBuffers % 100 == 0)
    {
        std::cout
            << "[GST][VIDEO DEMUX] buffer #"
            << e->videoDemuxBuffers
            << " size="
            << gst_buffer_get_size(buffer)
            << " PTS="
            << timeString(GST_BUFFER_PTS(buffer))
            << " DTS="
            << timeString(GST_BUFFER_DTS(buffer))
            << std::endl;
    }

    return GST_PAD_PROBE_OK;
}


// ============================================================
// AUDIO DEMUX PROBE
// ============================================================

static GstPadProbeReturn audioDemuxProbe(
    GstPad*,
    GstPadProbeInfo* info,
    gpointer userData)
{
    auto* e =
        static_cast<PipelineElements*>(userData);

    GstBuffer* buffer =
        GST_PAD_PROBE_INFO_BUFFER(info);

    if (!buffer)
        return GST_PAD_PROBE_OK;

    ++e->audioDemuxBuffers;

    if (e->audioDemuxBuffers == 1 ||
        e->audioDemuxBuffers % 100 == 0)
    {
        std::cout
            << "[GST][AUDIO DEMUX] buffer #"
            << e->audioDemuxBuffers
            << " size="
            << gst_buffer_get_size(buffer)
            << " PTS="
            << timeString(GST_BUFFER_PTS(buffer))
            << " DTS="
            << timeString(GST_BUFFER_DTS(buffer))
            << std::endl;
    }

    return GST_PAD_PROBE_OK;
}


// ============================================================
// H264 PARSER SINK
// ============================================================

static GstPadProbeReturn videoParseSinkProbe(
    GstPad*,
    GstPadProbeInfo* info,
    gpointer userData)
{
    auto* e =
        static_cast<PipelineElements*>(userData);

    GstBuffer* buffer =
        GST_PAD_PROBE_INFO_BUFFER(info);

    if (!buffer)
        return GST_PAD_PROBE_OK;

    ++e->videoParseBuffers;

    if (e->videoParseBuffers == 1 ||
        e->videoParseBuffers % 100 == 0)
    {
        std::cout
            << "[GST][H264 PARSE] buffer #"
            << e->videoParseBuffers
            << " size="
            << gst_buffer_get_size(buffer)
            << " PTS="
            << timeString(GST_BUFFER_PTS(buffer))
            << std::endl;
    }

    return GST_PAD_PROBE_OK;
}


// ============================================================
// H264 DECODER OUTPUT
// ============================================================

static GstPadProbeReturn videoDecoderProbe(
    GstPad*,
    GstPadProbeInfo* info,
    gpointer userData)
{
    auto* e =
        static_cast<PipelineElements*>(userData);

    GstBuffer* buffer =
        GST_PAD_PROBE_INFO_BUFFER(info);

    if (!buffer)
        return GST_PAD_PROBE_OK;

    ++e->videoDecodedBuffers;

    if (e->videoDecodedBuffers == 1 ||
        e->videoDecodedBuffers % 60 == 0)
    {
        std::cout
            << "[GST][DECODER] frame #"
            << e->videoDecodedBuffers
            << " size="
            << gst_buffer_get_size(buffer)
            << " PTS="
            << timeString(GST_BUFFER_PTS(buffer))
            << std::endl;
    }

    return GST_PAD_PROBE_OK;
}


// ============================================================
// VIDEO SINK
// ============================================================

static GstPadProbeReturn videoSinkProbe(
    GstPad*,
    GstPadProbeInfo* info,
    gpointer userData)
{
    auto* e =
        static_cast<PipelineElements*>(userData);

    GstBuffer* buffer =
        GST_PAD_PROBE_INFO_BUFFER(info);

    if (!buffer)
        return GST_PAD_PROBE_OK;

    ++e->videoSinkBuffers;

    if (e->videoSinkBuffers == 1 ||
        e->videoSinkBuffers % 60 == 0)
    {
        std::cout
            << "[GST][VIDEO SINK] frame #"
            << e->videoSinkBuffers
            << " size="
            << gst_buffer_get_size(buffer)
            << " PTS="
            << timeString(GST_BUFFER_PTS(buffer))
            << std::endl;
    }

    return GST_PAD_PROBE_OK;
}


// ============================================================
// AAC PARSER
// ============================================================

static GstPadProbeReturn audioParseSinkProbe(
    GstPad*,
    GstPadProbeInfo* info,
    gpointer userData)
{
    auto* e =
        static_cast<PipelineElements*>(userData);

    GstBuffer* buffer =
        GST_PAD_PROBE_INFO_BUFFER(info);

    if (!buffer)
        return GST_PAD_PROBE_OK;

    ++e->audioParseBuffers;

    if (e->audioParseBuffers == 1 ||
        e->audioParseBuffers % 100 == 0)
    {
        std::cout
            << "[GST][AAC PARSE] buffer #"
            << e->audioParseBuffers
            << " size="
            << gst_buffer_get_size(buffer)
            << " PTS="
            << timeString(GST_BUFFER_PTS(buffer))
            << std::endl;
    }

    return GST_PAD_PROBE_OK;
}


// ============================================================
// AAC DECODER
// ============================================================

static GstPadProbeReturn audioDecoderProbe(
    GstPad* pad,
    GstPadProbeInfo* info,
    gpointer userData)
{
    auto* e =
        static_cast<PipelineElements*>(userData);

    GstBuffer* buffer =
        GST_PAD_PROBE_INFO_BUFFER(info);

    if (!buffer)
        return GST_PAD_PROBE_OK;

    ++e->audioDecodedBuffers;

    if (e->audioDecodedBuffers == 1 ||
        e->audioDecodedBuffers % 100 == 0)
    {
        std::cout
            << "[GST][AAC DECODER] buffer #"
            << e->audioDecodedBuffers
            << " size="
            << gst_buffer_get_size(buffer)
            << " PTS="
            << timeString(GST_BUFFER_PTS(buffer))
            << std::endl;

        GstCaps* caps =
            gst_pad_get_current_caps(pad);

        if (caps)
        {
            gchar* capsString =
                gst_caps_to_string(caps);

            std::cout
                << "[GST][AUDIO CAPS] "
                << (capsString
                        ? capsString
                        : "unknown")
                << std::endl;

            g_free(capsString);
            gst_caps_unref(caps);
        }
    }

    return GST_PAD_PROBE_OK;
}


// ============================================================
// AUDIO SINK
// ============================================================

static GstPadProbeReturn audioSinkProbe(
    GstPad*,
    GstPadProbeInfo* info,
    gpointer userData)
{
    auto* e =
        static_cast<PipelineElements*>(userData);

    GstBuffer* buffer =
        GST_PAD_PROBE_INFO_BUFFER(info);

    if (!buffer)
        return GST_PAD_PROBE_OK;

    ++e->audioSinkBuffers;

    if (e->audioSinkBuffers == 1 ||
        e->audioSinkBuffers % 100 == 0)
    {
        std::cout
            << "[GST][AUDIO SINK] buffer #"
            << e->audioSinkBuffers
            << " size="
            << gst_buffer_get_size(buffer)
            << " PTS="
            << timeString(GST_BUFFER_PTS(buffer))
            << std::endl;
    }

    return GST_PAD_PROBE_OK;
}


// ============================================================
// AUDIO SINK CREATION
// ============================================================

static GstElement* createAudioSink()
{
    GstElement* sink =
        gst_element_factory_make(
            "pipewiresink",
            "audiosink");

    if (sink)
    {
        std::cout
            << "[GST] Audio sink: pipewiresink"
            << std::endl;

        return sink;
    }

    sink =
        gst_element_factory_make(
            "pulsesink",
            "audiosink");

    if (sink)
    {
        std::cout
            << "[GST] Audio sink: pulsesink"
            << std::endl;

        return sink;
    }

    sink =
        gst_element_factory_make(
            "alsasink",
            "audiosink");

    if (sink)
    {
        std::cout
            << "[GST] Audio sink: alsasink"
            << std::endl;

        return sink;
    }

    sink =
        gst_element_factory_make(
            "autoaudiosink",
            "audiosink");

    if (sink)
    {
        std::cout
            << "[GST] Audio sink: autoaudiosink"
            << std::endl;

        return sink;
    }

    return nullptr;
}


// ============================================================
// TSDEMUX PAD ADDED
// ============================================================

static void onPadAdded(
    GstElement*,
    GstPad* pad,
    gpointer userData)
{
    auto* e =
        static_cast<PipelineElements*>(userData);

    GstCaps* caps =
        gst_pad_get_current_caps(pad);

    if (!caps)
    {
        caps =
            gst_pad_query_caps(
                pad,
                nullptr);
    }

    if (!caps)
    {
        std::cerr
            << "[GST] Could not get demux caps"
            << std::endl;

        return;
    }

    gchar* capsString =
        gst_caps_to_string(caps);

    std::cout
        << "[GST] New demux pad: "
        << (capsString
                ? capsString
                : "unknown")
        << std::endl;

    GstStructure* structure =
        gst_caps_get_structure(
            caps,
            0);

    if (!structure)
    {
        g_free(capsString);
        gst_caps_unref(caps);
        return;
    }

    const gchar* mediaType =
        gst_structure_get_name(
            structure);


    // ========================================================
    // H264
    // ========================================================

    if (mediaType &&
        g_str_has_prefix(
            mediaType,
            "video/x-h264"))
    {
        std::cout
            << "[GST] H264 video stream detected"
            << std::endl;

        GstPad* sinkPad =
            gst_element_get_static_pad(
                e->h264parse,
                "sink");

        if (sinkPad &&
            !gst_pad_is_linked(sinkPad))
        {
            GstPadLinkReturn result =
                gst_pad_link(
                    pad,
                    sinkPad);

            if (result == GST_PAD_LINK_OK)
            {
                std::cout
                    << "[GST] Linked "
                       "tsdemux -> h264parse"
                    << std::endl;

                gst_pad_add_probe(
                    sinkPad,
                    GST_PAD_PROBE_TYPE_BUFFER,
                    videoParseSinkProbe,
                    e,
                    nullptr);

                GstPad* demuxPad = pad;

                gst_pad_add_probe(
                    demuxPad,
                    GST_PAD_PROBE_TYPE_BUFFER,
                    videoDemuxProbe,
                    e,
                    nullptr);
            }
            else
            {
                std::cerr
                    << "[GST] Video link failed: "
                    << gst_pad_link_get_name(result)
                    << std::endl;
            }
        }

        if (sinkPad)
            gst_object_unref(sinkPad);
    }


    // ========================================================
    // AAC
    // ========================================================

    else if (
        mediaType &&
        g_str_has_prefix(
            mediaType,
            "audio/mpeg"))
    {
        gint mpegVersion = 0;

        gst_structure_get_int(
            structure,
            "mpegversion",
            &mpegVersion);

        std::cout
            << "[GST] MPEG audio stream detected "
            << "mpegversion="
            << mpegVersion
            << std::endl;

        GstPad* sinkPad =
            gst_element_get_static_pad(
                e->aacparse,
                "sink");

        if (sinkPad &&
            !gst_pad_is_linked(sinkPad))
        {
            GstPadLinkReturn result =
                gst_pad_link(
                    pad,
                    sinkPad);

            if (result == GST_PAD_LINK_OK)
            {
                std::cout
                    << "[GST] Linked "
                       "tsdemux -> aacparse"
                    << std::endl;

                gst_pad_add_probe(
                    sinkPad,
                    GST_PAD_PROBE_TYPE_BUFFER,
                    audioParseSinkProbe,
                    e,
                    nullptr);

                gst_pad_add_probe(
                    pad,
                    GST_PAD_PROBE_TYPE_BUFFER,
                    audioDemuxProbe,
                    e,
                    nullptr);
            }
            else
            {
                std::cerr
                    << "[GST] Audio link failed: "
                    << gst_pad_link_get_name(result)
                    << std::endl;
            }
        }

        if (sinkPad)
            gst_object_unref(sinkPad);
    }

    g_free(capsString);
    gst_caps_unref(caps);
}

} // namespace


// ============================================================
// CONSTRUCTOR
// ============================================================

GStreamerPipeline::GStreamerPipeline()
    : elements_(new PipelineElements())
{
}


// ============================================================
// DESTRUCTOR
// ============================================================

GStreamerPipeline::~GStreamerPipeline()
{
    stop();

    delete elements_;
    elements_ = nullptr;
}


// ============================================================
// START
// ============================================================

bool GStreamerPipeline::start()
{
    PipelineElements* e =
        elements_;

    if (!e)
        return false;

    if (e->started.load())
        return true;


    gst_init(
        nullptr,
        nullptr);


    // ========================================================
    // CREATE CORE ELEMENTS
    // ========================================================

    e->pipeline =
        gst_pipeline_new(
            "miracast-pipeline");

    e->appsrc =
        gst_element_factory_make(
            "appsrc",
            "rtp-source");

    e->demux =
        gst_element_factory_make(
            "tsdemux",
            "tsdemux");


    // ========================================================
    // VIDEO
    // ========================================================

    e->h264parse =
        gst_element_factory_make(
            "h264parse",
            "h264parse");

    e->videoqueue =
        gst_element_factory_make(
            "queue",
            "videoqueue");

    e->videodecoder =
        gst_element_factory_make(
            "nvh264dec",
            "videodecoder");

    e->videosinkqueue =
        gst_element_factory_make(
            "queue",
            "videosinkqueue");

    e->videosink =
        gst_element_factory_make(
            "autovideosink",
            "videosink");


    // ========================================================
    // AUDIO
    // ========================================================

    e->aacparse =
        gst_element_factory_make(
            "aacparse",
            "aacparse");

    e->audioqueue =
        gst_element_factory_make(
            "queue",
            "audioqueue");

    e->audiodecoder =
        gst_element_factory_make(
            "avdec_aac",
            "audiodecoder");

    e->audioconvert =
        gst_element_factory_make(
            "audioconvert",
            "audioconvert");

    e->audioresample =
        gst_element_factory_make(
            "audioresample",
            "audioresample");

    e->audiocaps =
        gst_element_factory_make(
            "capsfilter",
            "audiocaps");

    e->audiosink =
        createAudioSink();


    // ========================================================
    // CHECK
    // ========================================================

    if (!e->pipeline ||
        !e->appsrc ||
        !e->demux ||

        !e->h264parse ||
        !e->videoqueue ||
        !e->videodecoder ||
        !e->videosinkqueue ||
        !e->videosink ||

        !e->aacparse ||
        !e->audioqueue ||
        !e->audiodecoder ||
        !e->audioconvert ||
        !e->audioresample ||
        !e->audiocaps ||
        !e->audiosink)
    {
        std::cerr
            << "[GST] Failed to create "
               "GStreamer elements"
            << std::endl;

        stop();

        return false;
    }


    // ========================================================
    // APPSRC
    // ========================================================

    g_object_set(
        e->appsrc,

        "is-live",
        TRUE,

        "format",
        GST_FORMAT_TIME,

        "block",
        FALSE,

        "do-timestamp",
        FALSE,

        "max-bytes",
        4 * 1024 * 1024,

        nullptr);


    // ========================================================
    // MPEG-TS CAPS
    // ========================================================

    GstCaps* tsCaps =
        gst_caps_new_simple(
            "video/mpegts",

            "systemstream",
            G_TYPE_BOOLEAN,
            TRUE,

            "packetsize",
            G_TYPE_INT,
            188,

            nullptr);

    gst_app_src_set_caps(
        GST_APP_SRC(e->appsrc),
        tsCaps);

    gst_caps_unref(tsCaps);


    std::cout
        << "[GST] appsrc caps: "
           "video/mpegts, "
           "systemstream=true, "
           "packetsize=188"
        << std::endl;


    // ========================================================
    // H264 PARSER
    // ========================================================

    g_object_set(
        e->h264parse,

        "config-interval",
        -1,

        nullptr);


    // ========================================================
    // VIDEO QUEUE
    // ========================================================

    g_object_set(
        e->videoqueue,

        "max-size-buffers",
        30,

        "max-size-bytes",
        0,

        "max-size-time",
        500 * GST_MSECOND,

        "leaky",
        2,

        nullptr);


    // ========================================================
    // VIDEO SINK QUEUE
    // ========================================================

    g_object_set(
        e->videosinkqueue,

        "max-size-buffers",
        10,

        "max-size-bytes",
        0,

        "max-size-time",
        300 * GST_MSECOND,

        "leaky",
        2,

        nullptr);


    // ========================================================
    // VIDEO SINK
    // ========================================================

    g_object_set(
        e->videosink,

        "sync",
        FALSE,

        nullptr);


    // ========================================================
    // AUDIO QUEUE
    // ========================================================

    g_object_set(
        e->audioqueue,

        "max-size-buffers",
        100,

        "max-size-bytes",
        0,

        "max-size-time",
        500 * GST_MSECOND,

        "leaky",
        2,

        nullptr);


    // ========================================================
    // AUDIO RESAMPLER
    // ========================================================

    g_object_set(
        e->audioresample,

        "quality",
        10,

        nullptr);


    // ========================================================
    // AUDIO CAPS
    //
    // Force the decoded PCM going into PipeWire to:
    //
    //     S16LE
    //     48000 Hz
    //     2 channels
    //
    // audioresample will perform the actual conversion if
    // avdec_aac produces another sample rate.
    // ========================================================

    GstCaps* audioCaps =
        gst_caps_new_simple(
            "audio/x-raw",

            "format",
            G_TYPE_STRING,
            "S16LE",

            "rate",
            G_TYPE_INT,
            48000,

            "channels",
            G_TYPE_INT,
            2,

            nullptr);

    g_object_set(
        e->audiocaps,

        "caps",
        audioCaps,

        nullptr);

    gst_caps_unref(audioCaps);


    std::cout
        << "[GST] Audio output: "
           "S16LE, 48000 Hz, stereo"
        << std::endl;


    // ========================================================
    // AUDIO SINK
    // ========================================================

    g_object_set(
        e->audiosink,

        "sync",
        FALSE,

        nullptr);


    // ========================================================
    // ADD TO PIPELINE
    // ========================================================

    gst_bin_add_many(
        GST_BIN(e->pipeline),

        e->appsrc,
        e->demux,

        e->h264parse,
        e->videoqueue,
        e->videodecoder,
        e->videosinkqueue,
        e->videosink,

        e->aacparse,
        e->audioqueue,
        e->audiodecoder,
        e->audioconvert,
        e->audioresample,
        e->audiocaps,
        e->audiosink,

        nullptr);


    // ========================================================
    // VIDEO LINK
    // ========================================================

    if (!gst_element_link_many(
            e->h264parse,
            e->videoqueue,
            e->videodecoder,
            e->videosinkqueue,
            e->videosink,
            nullptr))
    {
        std::cerr
            << "[GST] Failed to link video"
            << std::endl;

        stop();

        return false;
    }


    // ========================================================
    // AUDIO LINK
    // ========================================================

    if (!gst_element_link_many(
            e->aacparse,
            e->audioqueue,
            e->audiodecoder,
            e->audioconvert,
            e->audioresample,
            e->audiocaps,
            e->audiosink,
            nullptr))
    {
        std::cerr
            << "[GST] Failed to link audio"
            << std::endl;

        stop();

        return false;
    }


    // ========================================================
    // APPSRC -> TSDEMUX
    // ========================================================

    if (!gst_element_link(
            e->appsrc,
            e->demux))
    {
        std::cerr
            << "[GST] Failed to link "
               "appsrc -> tsdemux"
            << std::endl;

        stop();

        return false;
    }


    // ========================================================
    // DYNAMIC PADS
    // ========================================================

    g_signal_connect(
        e->demux,
        "pad-added",
        G_CALLBACK(onPadAdded),
        e);


    // ========================================================
    // VIDEO DECODER PROBE
    // ========================================================

    GstPad* pad =
        gst_element_get_static_pad(
            e->videodecoder,
            "src");

    if (pad)
    {
        gst_pad_add_probe(
            pad,
            GST_PAD_PROBE_TYPE_BUFFER,
            videoDecoderProbe,
            e,
            nullptr);

        gst_object_unref(pad);
    }


    // ========================================================
    // VIDEO SINK PROBE
    // ========================================================

    pad =
        gst_element_get_static_pad(
            e->videosinkqueue,
            "src");

    if (pad)
    {
        gst_pad_add_probe(
            pad,
            GST_PAD_PROBE_TYPE_BUFFER,
            videoSinkProbe,
            e,
            nullptr);

        gst_object_unref(pad);
    }


    // ========================================================
    // AUDIO DECODER PROBE
    // ========================================================

    pad =
        gst_element_get_static_pad(
            e->audiodecoder,
            "src");

    if (pad)
    {
        gst_pad_add_probe(
            pad,
            GST_PAD_PROBE_TYPE_BUFFER,
            audioDecoderProbe,
            e,
            nullptr);

        gst_object_unref(pad);
    }


    // ========================================================
    // AUDIO SINK PROBE
    // ========================================================

    pad =
        gst_element_get_static_pad(
            e->audiosink,
            "sink");

    if (pad)
    {
        gst_pad_add_probe(
            pad,
            GST_PAD_PROBE_TYPE_BUFFER,
            audioSinkProbe,
            e,
            nullptr);

        gst_object_unref(pad);
    }


    // ========================================================
    // BUS
    // ========================================================

    GstBus* bus =
        gst_element_get_bus(
            e->pipeline);

    gst_bus_set_sync_handler(
        bus,

        [](GstBus*,
           GstMessage* message,
           gpointer) -> GstBusSyncReply
        {
            switch (GST_MESSAGE_TYPE(message))
            {
                case GST_MESSAGE_ERROR:
                {
                    GError* error = nullptr;
                    gchar* debug = nullptr;

                    gst_message_parse_error(
                        message,
                        &error,
                        &debug);

                    std::cerr
                        << "[GST][ERROR] "
                        << (error
                                ? error->message
                                : "unknown")
                        << std::endl;

                    if (debug)
                    {
                        std::cerr
                            << "[GST][DEBUG] "
                            << debug
                            << std::endl;
                    }

                    if (error)
                        g_error_free(error);

                    if (debug)
                        g_free(debug);

                    break;
                }

                case GST_MESSAGE_WARNING:
                {
                    GError* error = nullptr;
                    gchar* debug = nullptr;

                    gst_message_parse_warning(
                        message,
                        &error,
                        &debug);

                    std::cerr
                        << "[GST][WARNING] "
                        << (error
                                ? error->message
                                : "unknown")
                        << std::endl;

                    if (debug)
                    {
                        std::cerr
                            << "[GST][DEBUG] "
                            << debug
                            << std::endl;
                    }

                    if (error)
                        g_error_free(error);

                    if (debug)
                        g_free(debug);

                    break;
                }

                default:
                    break;
            }

            return GST_BUS_PASS;
        },

        nullptr,
        nullptr);

    gst_object_unref(bus);


    // ========================================================
    // START
    // ========================================================

    GstStateChangeReturn result =
        gst_element_set_state(
            e->pipeline,
            GST_STATE_PLAYING);

    if (result == GST_STATE_CHANGE_FAILURE)
    {
        std::cerr
            << "[GST] Failed to start pipeline"
            << std::endl;

        stop();

        return false;
    }


    if (result == GST_STATE_CHANGE_ASYNC)
    {
        GstStateChangeReturn stateResult =
            gst_element_get_state(
                e->pipeline,
                nullptr,
                nullptr,
                2 * GST_SECOND);

        if (stateResult == GST_STATE_CHANGE_FAILURE)
        {
            std::cerr
                << "[GST] Pipeline failed "
                   "during state transition"
                << std::endl;

            stop();

            return false;
        }
    }


    e->started.store(true);


    std::cout
        << "[GST] Pipeline started"
        << std::endl;

    std::cout
        << "[GST] Video decoder: nvh264dec"
        << std::endl;

    std::cout
        << "[GST] Video sink: autovideosink"
        << std::endl;

    std::cout
        << "[GST] Video sync: FALSE"
        << std::endl;

    std::cout
        << "[GST] Audio decoder: avdec_aac"
        << std::endl;

    std::cout
        << "[GST] Audio output: "
           "S16LE / 48000 Hz / stereo"
        << std::endl;

    std::cout
        << "[GST] Audio sync: FALSE"
        << std::endl;


    return true;
}


// ============================================================
// PUSH MPEG-TS
// ============================================================

bool GStreamerPipeline::push(
    const uint8_t* data,
    size_t size)
{
    PipelineElements* e =
        elements_;

    if (!e ||
        !e->started.load() ||
        !e->appsrc ||
        !data ||
        size == 0)
    {
        return false;
    }


    GstBuffer* buffer =
        gst_buffer_new_allocate(
            nullptr,
            size,
            nullptr);

    if (!buffer)
        return false;


    GstMapInfo map;

    if (!gst_buffer_map(
            buffer,
            &map,
            GST_MAP_WRITE))
    {
        gst_buffer_unref(buffer);

        return false;
    }


    std::memcpy(
        map.data,
        data,
        size);


    gst_buffer_unmap(
        buffer,
        &map);


    GstFlowReturn result =
        gst_app_src_push_buffer(
            GST_APP_SRC(e->appsrc),
            buffer);


    if (result != GST_FLOW_OK)
    {
        std::cerr
            << "[GST] appsrc push failed: "
            << gst_flow_get_name(result)
            << std::endl;

        return false;
    }


    return true;
}


// ============================================================
// STOP
// ============================================================

void GStreamerPipeline::stop()
{
    PipelineElements* e =
        elements_;

    if (!e)
        return;

    if (!e->pipeline)
        return;

    if (!e->started.exchange(false))
        return;


    std::cout
        << "[GST] Stopping pipeline..."
        << std::endl;


    if (e->appsrc)
    {
        gst_app_src_end_of_stream(
            GST_APP_SRC(e->appsrc));
    }


    gst_element_set_state(
        e->pipeline,
        GST_STATE_NULL);


    gst_object_unref(
        e->pipeline);


    e->pipeline = nullptr;

    e->appsrc = nullptr;
    e->demux = nullptr;

    e->h264parse = nullptr;
    e->videoqueue = nullptr;
    e->videodecoder = nullptr;
    e->videosinkqueue = nullptr;
    e->videosink = nullptr;

    e->aacparse = nullptr;
    e->audioqueue = nullptr;
    e->audiodecoder = nullptr;
    e->audioconvert = nullptr;
    e->audioresample = nullptr;
    e->audiocaps = nullptr;
    e->audiosink = nullptr;


    std::cout
        << "[GST] Pipeline stopped"
        << std::endl;
}