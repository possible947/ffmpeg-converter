#include "runtime_probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

int main(void)
{
    char path[] = "/tmp/ffmpeg_converter_probe_catalog_XXXXXX";
    const char *json =
        "{\n"
        "  \"selection\": {\n"
        "    \"platforms\": {\n"
        "      \"linux\": {\n"
        "        \"hwaccel\": {\n"
        "          \"enabled\": true,\n"
        "          \"groups\": {\n"
        "            \"vaapi\": {\n"
        "              \"enabled\": true,\n"
        "              \"encoders\": {\n"
        "                \"h264\": {\"enabled\": true, \"final_codec\": \"h264_vaapi\"},\n"
        "                \"hevc\": {\"enabled\": false, \"final_codec\": \"hevc_vaapi\"}\n"
        "              }\n"
        "            },\n"
        "            \"disabled\": {\n"
        "              \"enabled\": false,\n"
        "              \"encoders\": {\"h264\": {\"enabled\": true, \"final_codec\": \"h264_vaapi\"}}\n"
        "            }\n"
        "          }\n"
        "        }\n"
        "      },\n"
        "      \"windows\": {\n"
        "        \"hwaccel\": {\"enabled\": true, \"groups\": {}}\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}\n";
    int fd;
    FILE *file;

    fd = mkstemp(path);
    expect(fd >= 0, "create temporary catalog");
    if (fd < 0)
        return 1;

    file = fdopen(fd, "w");
    expect(file != NULL, "open temporary catalog");
    if (!file) {
        close(fd);
        unlink(path);
        return 1;
    }
    fputs(json, file);
    fclose(file);

    expect(linux_probe_catalog_component_enabled(path, "linux", "vaapi", "h264", "h264_vaapi"),
           "enabled component is accepted");
    expect(!linux_probe_catalog_component_enabled(path, "linux", "vaapi", "hevc", "hevc_vaapi"),
           "disabled component is rejected");
    expect(!linux_probe_catalog_component_enabled(path, "linux", "disabled", "h264", "h264_vaapi"),
           "disabled group is rejected");
    expect(!linux_probe_catalog_component_enabled(path, "windows", "vaapi", "h264", "h264_vaapi"),
           "component from another platform is rejected");
    expect(!linux_probe_catalog_component_enabled(path, "linux", "vaapi", "h264", "wrong_codec"),
           "final codec mismatch is rejected");

    unlink(path);
    if (failures != 0)
        return 1;

    puts("runtime probe catalog tests passed");
    return 0;
}
