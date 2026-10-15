#include "subtitle_preservation.h"
#include "converter_platform.h"

#include <jansson.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int subtitle_preservation_build(const char *input, MuxContainer container,
                                char *args, size_t args_size,
                                const ConverterCallbacks *callbacks)
{
    const char *probe = platform_get_ffprobe_bin();
    char *tool;
    char *path;
    char command[8192];
    char line[256];
    FILE *pipe;
    json_t *document;
    json_t *streams;
    json_t *stream;
    size_t i;
    size_t used = 0;
    unsigned int output_index = 0;
    int status;

    if (!args || !args_size || !input || !probe || !probe[0])
        return 0;
    args[0] = '\0';
    tool = platform_escape_path_for_command(probe);
    path = platform_escape_path_for_command(input);
    if (!tool || !path) {
        free(tool);
        free(path);
        return 0;
    }
    status = snprintf(command, sizeof(command),
                      "%s -v error -select_streams s "
                      "-show_entries stream=index,codec_name -of json %s 2>%s",
                      tool, path, platform_get_null_device());
    free(tool);
    free(path);
    if (status < 0 || (size_t)status >= sizeof(command))
        return 0;
    pipe = platform_popen(command, "r");
    if (!pipe)
        return 0;
    document = json_loadf(pipe, 0, NULL);
    /* Drain even malformed output before waiting for the child. */
    while (fgets(line, sizeof(line), pipe)) {}
    status = platform_pclose(pipe);
    streams = json_object_get(document, "streams");
    if (status != 0 || !json_is_array(streams)) {
        json_decref(document);
        return 0;
    }
    json_array_foreach(streams, i, stream) {
        json_t *index = json_object_get(stream, "index");
        const char *codec = json_string_value(json_object_get(stream, "codec_name"));
        MuxSubtitlePolicy policy = mux_subtitle_policy_resolve(codec, container);
        int written;
        if (!json_is_integer(index) || json_integer_value(index) < 0 ||
            json_integer_value(index) > INT_MAX) {
            json_decref(document);
            return 0;
        }
        if (policy.action != MUX_SUBTITLE_COPY && callbacks && callbacks->on_message) {
            char warning[256];
            snprintf(warning, sizeof(warning),
                     "Warning: subtitle stream %d (%s): %s for %s%s%s.",
                     (int)json_integer_value(index), codec ? codec : "unknown",
                     policy.action == MUX_SUBTITLE_DROP ? "dropped (unsupported)" : "converted",
                     mux_container_name(container),
                     policy.encoder ? " using " : "",
                     policy.encoder ? policy.encoder : "");
            callbacks->on_message(warning);
        }
        if (policy.action == MUX_SUBTITLE_DROP)
            continue;
        written = snprintf(args + used, args_size - used,
                           "-map 0:%d -c:s:%u %s ",
                           (int)json_integer_value(index), output_index++,
                           policy.encoder);
        if (written < 0 || (size_t)written >= args_size - used) {
            json_decref(document);
            args[0] = '\0';
            return 0;
        }
        used += (size_t)written;
    }
    json_decref(document);
    return 1;
}
