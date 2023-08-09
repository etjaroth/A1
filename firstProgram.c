#include <gst/gst.h>

typedef struct _PipelineData
{
    GstElement *pipeline;
    GstElement *source;

    GstElement *video_convert;
    GstElement *video_resize;    
    GstElement *video_resize_capsfilter;    
    GstElement *video_rate;    

    GstElement *encoder;
    GstElement *payloader;
    GstElement *udp_sink;
} PipelineData;

// Dynamic Pad linking callback
void pad_added_handler(GstElement *src, GstPad *new_pad, PipelineData *data)
{
    GstPad *video_sink_pad = gst_element_get_static_pad(data->video_convert, "sink");
    GstPadLinkReturn ret;

    g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

    if (gst_pad_is_linked(video_sink_pad))
    {
        g_print("We are already linked. Ignoring.\n");
    }
    else
    {
        // Check the new pad's type
        GstCaps *new_pad_caps = gst_pad_get_current_caps(new_pad);
        GstStructure *new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
        const gchar *new_pad_type = gst_structure_get_name(new_pad_struct);

        if (g_str_has_prefix(new_pad_type, "video/x-raw"))
        {
            // Try linking
            ret = gst_pad_link(new_pad, video_sink_pad);
            if (GST_PAD_LINK_FAILED(ret))
            {
                g_print("Type is '%s' but link failed.\n", new_pad_type);
            }
            else
            {
                g_print("Link succeeded (type '%s').\n", new_pad_type);
            }
        }
        else
        {
            g_print("It has type '%s' which is not raw video. Ignoring.\n", new_pad_type);
        }
    }

    // Release sink pad
    gst_object_unref(video_sink_pad);
}

int main(int argc, char *argv[])
{
    // Initialize GStreamer
    gst_init(&argc, &argv);
    PipelineData data;
    GstStateChangeReturn ret;

    // Create the elements
    data.source = gst_element_factory_make("uridecodebin", "source");
    data.video_convert = gst_element_factory_make("videoconvert", "video_convert");
    data.video_resize = gst_element_factory_make("videoscale", "video_resize");
    data.video_resize_capsfilter = gst_element_factory_make("capsfilter", "video_resize_capsfilter");
    data.video_rate = gst_element_factory_make("videorate", "video_rate");

    data.encoder = gst_element_factory_make("x264enc", "encoder");
    data.payloader = gst_element_factory_make("rtph264pay", "payloader");
    data.udp_sink = gst_element_factory_make("udpsink", "udp_sink");

    // Resize video (I'm writing this on a linux vm and I don't have enough video memory to dispay the full resolution
    // since VirtualBox limits the maximum allocatable vram)
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                         "width", G_TYPE_INT, 640, // 360p
                                         "height", G_TYPE_INT, 360,
                                         NULL);
    g_object_set(data.video_resize_capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    g_object_set(data.udp_sink, "host", "localhost", "port", 5004, NULL); // This line shouldn't actually change anything (it just (re)sets default settings)

    // Create the pipeline
    data.pipeline = gst_pipeline_new("data-pipeline");
    if (!data.pipeline ||
        !data.source ||
        !data.video_convert ||
        !data.video_resize ||
        !data.video_resize_capsfilter ||
        !data.video_rate ||
        !data.udp_sink)
    {
        g_printerr("Not all elements could be created.\n");
        g_printerr("%d%d%d%d%d%d%d%d%d\n",
                   (int)(0 == data.pipeline),
                   (int)(0 == data.source),
                   (int)(0 == data.video_convert),
                   (int)(0 == data.video_resize),
                   (int)(0 == data.video_resize_capsfilter),
                   (int)(0 == data.video_rate),
                   (int)(0 == data.encoder),
                   (int)(0 == data.payloader),
                   (int)(0 == data.udp_sink));
        return -1;
    }

    // Add to the pipeline
    gst_bin_add(GST_BIN(data.pipeline), data.source); // Source
    gst_bin_add_many(GST_BIN(data.pipeline), // UDP (Video)
                     data.video_convert,
                     data.video_resize,
                     data.video_resize_capsfilter,
                     data.video_rate,
                     data.encoder,
                     data.payloader,
                     data.udp_sink,
                     NULL);

    // Link UDP (Video)
    if (!gst_element_link_many(data.video_convert, data.video_resize, data.video_resize_capsfilter, data.video_rate, data.encoder, data.payloader, data.udp_sink, NULL))
    {
        g_printerr("Video elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    // Set the URI to play
    g_object_set(data.source, "uri", "https://media.githubusercontent.com/media/Haxerus/test-video-repo/master/test_video.webm", NULL);

    // Add pad_added_handler callback (dynamic pad linknig)
    g_signal_connect(data.source, "pad-added", G_CALLBACK(pad_added_handler), &data);

    // Start playing pipeline
    ret = gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        g_printerr("Unable to set the pipeline to the playing state.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    // Listen to the bus
    GstBus *bus = gst_element_get_bus(data.pipeline);
    gboolean terminate = FALSE;
    do
    {
        GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                                     GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

        // Parse message
        if (msg != NULL)
        {
            GError *err;
            gchar *debug_info;

            switch (GST_MESSAGE_TYPE(msg))
            {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &err, &debug_info);
                g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
                g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
                g_clear_error(&err);
                g_free(debug_info);
                terminate = TRUE;
                break;
            case GST_MESSAGE_EOS:
                g_print("End-Of-Stream reached.\n");
                terminate = TRUE;
                break;
            case GST_MESSAGE_STATE_CHANGED:
                // We are only interested in state-changed messages from the pipeline
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data.pipeline))
                {
                    GstState old_state, new_state, pending_state;
                    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
                    g_print("Pipeline state changed from %s to %s:\n",
                            gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
                }
                break;
            default:
                // Should not happen
                g_printerr("Unexpected message received.\n");
                break;
            }
            gst_message_unref(msg);
        }
    } while (!terminate);

    // Release resources
    gst_object_unref(bus);
    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(data.pipeline);
    return 0;
}
