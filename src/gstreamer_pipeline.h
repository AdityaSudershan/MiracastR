#ifndef GSTREAMER_PIPELINE_H
#define GSTREAMER_PIPELINE_H

#include <cstddef>
#include <cstdint>

struct PipelineElements;

class GStreamerPipeline
{
public:
    GStreamerPipeline();
    ~GStreamerPipeline();

    bool start();
    bool push(const uint8_t* data, size_t size);
    void stop();

private:
    PipelineElements* elements_;
};

#endif
