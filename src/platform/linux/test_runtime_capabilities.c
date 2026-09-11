#include "runtime_probe.h"

#include <stdio.h>

int main(void)
{
    LinuxCodecSupport support;

    if (!linux_probe_codec_support(&support))
        return 1;
    printf("vaapi: h264=%d hevc=%d av1=%d hevc_10bit=%d av1_10bit=%d\n",
           support.has_h264_vaapi,
           support.has_hevc_vaapi,
           support.has_av1_vaapi,
           support.has_hevc_vaapi_10bit,
           support.has_av1_vaapi_10bit);
    printf("qsv: h264=%d hevc=%d av1=%d hevc_10bit=%d av1_10bit=%d\n",
           support.has_h264_qsv,
           support.has_hevc_qsv,
           support.has_av1_qsv,
           support.has_hevc_qsv_10bit,
           support.has_av1_qsv_10bit);
    printf("default_render_node=%s\n", support.default_render_node);
    return 0;
}
