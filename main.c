#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
//#include <vdpau/vdpau.h>
#include <vdpau/vdpau_x11.h>

#include "vdpau_layer.h"

#define WIDTH 1280
#define HEIGHT 720

static Display *x11_display;
static int x11_screen;
static Window x11_window;
static GC x11_gc;

// VDPAU variables
static VdpDevice vdp_device;
static VdpDecoder vdp_decoder;
static VdpVideoSurface vdp_video_surface;
static VdpOutputSurface vdp_output_surface;
static VdpPresentationQueueTarget vdp_target;
static VdpPresentationQueue vdp_queue;
static uint32_t vid_width, vid_height;
static VdpChromaType vdp_chroma_type;
static VdpVideoMixer vdp_video_mixer;


// VDPAU functions (That will be dynamically linked)

VdpDeviceDestroy                                *vdp_device_destroy;
VdpGetProcAddress                               *vdp_get_proc_address;
VdpGetErrorString                               *vdp_get_error_string;

VdpVideoSurfaceCreate                           *vdp_video_surface_create;
VdpVideoSurfaceDestroy                          *vdp_video_surface_destroy;
VdpVideoSurfaceQueryCapabilities                *vdp_video_surface_query_capabilities;
VdpVideoSurfaceQueryGetPutBitsYCbCrCapabilities *vdp_video_surface_query_ycbcr_capabilities;
VdpVideoSurfaceGetParameters                    *vdp_video_surface_get_parameters;
VdpVideoSurfaceGetBitsYCbCr                     *vdp_video_surface_get_bits_ycbcr;
VdpVideoSurfacePutBitsYCbCr                     *vdp_video_surface_put_bits_ycbcr;

VdpDecoderCreate                                *vdp_decoder_create;
VdpDecoderDestroy                               *vdp_decoder_destroy;
VdpDecoderRender                                *vdp_decoder_render;
VdpDecoderQueryCapabilities                     *vdp_decoder_query_capabilities;
VdpDecoderGetParameters                         *vdp_decoder_get_parameters;

VdpVideoMixerCreate                             *vdp_video_mixer_create;
VdpVideoMixerDestroy                            *vdp_video_mixer_destroy;
VdpVideoMixerRender                             *vdp_video_mixer_render;
VdpVideoMixerSetFeatureEnables                  *vdp_video_mixer_set_feature_enables;
VdpVideoMixerSetAttributeValues                 *vdp_video_mixer_set_attribute_values;

VdpOutputSurfaceCreate                          *vdp_output_surface_create;
VdpOutputSurfaceDestroy                         *vdp_output_surface_destroy;
VdpOutputSurfaceQueryCapabilities               *vdp_output_surface_query_capabilities;
VdpOutputSurfaceGetBitsNative                   *vdp_output_surface_get_bits_native;

VdpPresentationQueueTargetCreateX11             *vdp_presentation_queue_target_create_x11;
VdpPresentationQueueTargetDestroy               *vdp_presentation_queue_target_destroy;

VdpPresentationQueueCreate                      *vdp_presentation_queue_create;
VdpPresentationQueueDestroy                     *vdp_presentation_queue_destroy;
VdpPresentationQueueDisplay                     *vdp_presentation_queue_display;
VdpPresentationQueueBlockUntilSurfaceIdle       *vdp_presentation_queue_block_until_surface_idle;
VdpPresentationQueueSetBackgroundColor          *vdp_presentation_queue_set_background_color;
VdpPresentationQueueQuerySurfaceStatus          *vdp_presentation_queue_query_surface_status;

VdpGetInformationString                         *vdp_get_information_string;

int init_x11() {
    unsigned long black, white;
    x11_display = XOpenDisplay((char*)0); //open display 0
    if (!x11_display) {
        fprintf(stderr, "Failed to open XDisplay\n");
        return -1;
    }
    x11_screen = DefaultScreen(x11_display);
    black = BlackPixel(x11_display, x11_screen);
    white = WhitePixel(x11_display, x11_screen);
    x11_window = XCreateSimpleWindow(x11_display, DefaultRootWindow(x11_display), 0, 0,
                                300, 300, 5, black, white);
    XSetStandardProperties(x11_display, x11_window, "VDPAU Test", "VDPAU", 
                            None, NULL, 0, NULL);
    XSelectInput(x11_display,x11_window,ExposureMask|ButtonPressMask|KeyPressMask);
    x11_gc = XCreateGC(x11_display, x11_window, 0, NULL);
    XSetBackground(x11_display, x11_gc, white);
    XSetBackground(x11_display, x11_gc, black);
    XClearWindow(x11_display, x11_window);    
    XMapWindow(x11_display, x11_window);
    printf("X11 Display created\n");
    return 0;
}

VdpStatus init_vdpau() {
    VdpStatus retval = VDP_STATUS_OK;

    struct VdpFunction {
        const int id;
        void *pointer;
    };

    struct VdpFunction vdp_function[] = {
        {VDP_FUNC_ID_DEVICE_DESTROY, &vdp_device_destroy},
        {VDP_FUNC_ID_VIDEO_SURFACE_CREATE,
            &vdp_video_surface_create},
        {VDP_FUNC_ID_VIDEO_SURFACE_DESTROY,
            &vdp_video_surface_destroy},
        {VDP_FUNC_ID_VIDEO_SURFACE_QUERY_CAPABILITIES,
            &vdp_video_surface_query_capabilities},
        {VDP_FUNC_ID_VIDEO_SURFACE_QUERY_GET_PUT_BITS_Y_CB_CR_CAPABILITIES,
            &vdp_video_surface_query_ycbcr_capabilities},
        {VDP_FUNC_ID_VIDEO_SURFACE_GET_BITS_Y_CB_CR,
            &vdp_video_surface_get_bits_ycbcr},
        {VDP_FUNC_ID_VIDEO_SURFACE_PUT_BITS_Y_CB_CR,
            &vdp_video_surface_put_bits_ycbcr},
        {VDP_FUNC_ID_VIDEO_SURFACE_GET_PARAMETERS,
            &vdp_video_surface_get_parameters},
        {VDP_FUNC_ID_DECODER_CREATE, &vdp_decoder_create},
        {VDP_FUNC_ID_DECODER_RENDER, &vdp_decoder_render},
        {VDP_FUNC_ID_DECODER_DESTROY, &vdp_decoder_destroy},
        {VDP_FUNC_ID_DECODER_QUERY_CAPABILITIES,
            &vdp_decoder_query_capabilities},
        {VDP_FUNC_ID_DECODER_GET_PARAMETERS,
            &vdp_decoder_get_parameters},
        {VDP_FUNC_ID_VIDEO_MIXER_CREATE, &vdp_video_mixer_create},
        {VDP_FUNC_ID_VIDEO_MIXER_DESTROY, &vdp_video_mixer_destroy},
        {VDP_FUNC_ID_VIDEO_MIXER_RENDER, &vdp_video_mixer_render},
        {VDP_FUNC_ID_VIDEO_MIXER_SET_FEATURE_ENABLES,
            &vdp_video_mixer_set_feature_enables},
        {VDP_FUNC_ID_VIDEO_MIXER_SET_ATTRIBUTE_VALUES,
            &vdp_video_mixer_set_attribute_values},
        {VDP_FUNC_ID_OUTPUT_SURFACE_CREATE, &vdp_output_surface_create},
        {VDP_FUNC_ID_OUTPUT_SURFACE_DESTROY, &vdp_output_surface_destroy},
        {VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_CAPABILITIES,
            &vdp_output_surface_query_capabilities},
        {VDP_FUNC_ID_OUTPUT_SURFACE_GET_BITS_NATIVE,
            &vdp_output_surface_get_bits_native},
        {VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_CREATE_X11,
            &vdp_presentation_queue_target_create_x11},
        {VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_DESTROY,
            &vdp_presentation_queue_target_destroy},
        {VDP_FUNC_ID_PRESENTATION_QUEUE_CREATE,
            &vdp_presentation_queue_create},
        {VDP_FUNC_ID_PRESENTATION_QUEUE_DESTROY,
            &vdp_presentation_queue_destroy},
        {VDP_FUNC_ID_PRESENTATION_QUEUE_DISPLAY,
            &vdp_presentation_queue_display},
        {VDP_FUNC_ID_PRESENTATION_QUEUE_BLOCK_UNTIL_SURFACE_IDLE,
            &vdp_presentation_queue_block_until_surface_idle},
        {VDP_FUNC_ID_PRESENTATION_QUEUE_SET_BACKGROUND_COLOR,
            &vdp_presentation_queue_set_background_color},
        {VDP_FUNC_ID_PRESENTATION_QUEUE_QUERY_SURFACE_STATUS,
            &vdp_presentation_queue_query_surface_status},
        {VDP_FUNC_ID_GET_INFORMATION_STRING, &vdp_get_information_string},
        {0,NULL}
    };

    retval = vdp_device_create_x11(x11_display, x11_screen, &vdp_device,&vdp_get_proc_address);
    if (retval != VDP_STATUS_OK) {
        fprintf(stderr, "Failed to create X11 device\n");
        return -1;
    }

    fprintf(stdout,"X11 device created\n");
    const struct VdpFunction *dsc;
    for (dsc = vdp_function; dsc->pointer; dsc++) {
        retval = vdp_get_proc_address(vdp_device, dsc->id,dsc->pointer);
        if (retval != VDP_STATUS_OK) {
            fprintf(stderr,"Failed to link function id: %d\n",dsc->id);
            return retval;
        } 
    }
    fprintf(stdout,"Linked all functions\n");

    return retval;
}

int init_decoder() {
    VdpStatus retval = VDP_STATUS_OK;
    retval = vdp_decoder_create(vdp_device,VDP_DECODER_PROFILE_H264_MAIN,
                                WIDTH,HEIGHT,2,&vdp_decoder);
    if (retval != VDP_STATUS_OK) {
        fprintf(stderr,"Decoder create failed with error %d\n",retval);
        return retval;
    }
    fprintf(stdout,"Decoder created\n");
    return retval;
}

int init_surfaces() {
    VdpStatus vdpret;
    vdpret = vdp_video_surface_create(vdp_device, VDP_CHROMA_TYPE_420,
                                      WIDTH,HEIGHT,&vdp_video_surface);
    if (vdpret != VDP_STATUS_OK) {
        fprintf(stderr,"Failed to create video surface\n");
        return vdpret;
    }

    vdpret = vdp_output_surface_create(vdp_device, VDP_RGBA_FORMAT_B8G8R8A8,
                                WIDTH,HEIGHT, &vdp_output_surface);
    if (vdpret != VDP_STATUS_OK) {
        fprintf(stderr, "Failed to create output surface: %d\n",vdpret);
        return vdpret;
    }
    return vdpret;
}

int init_video_mixer() {
#define VDP_NUM_MIXER_PARAMETER 3
#define MAX_NUM_FEATURES 6
    VdpStatus vdpret;
    int feature_count = 0;
    VdpVideoMixerFeature features[MAX_NUM_FEATURES];
    VdpBool features_enables[MAX_NUM_FEATURES];

    static const VdpVideoMixerParameter parameters[VDP_NUM_MIXER_PARAMETER] = {
        VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH,
        VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT,
        VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE
    };

    const void *const parameter_value[VDP_NUM_MIXER_PARAMETER] = {
        &vid_width,
        &vid_height,
        &vdp_chroma_type 
    };
    vdpret = vdp_video_mixer_create(vdp_device, feature_count, features,
                                    VDP_NUM_MIXER_PARAMETER, parameters,
                                    parameter_value, &vdp_video_mixer);
    if (vdpret != VDP_STATUS_OK) {
        fprintf(stderr,"Failed to create mixer %d\n", vdpret);
        return vdpret;
    }
    fprintf(stdout,"Created videomixer\n");
    return vdpret;
}

int init_presentation_queue() {
    VdpStatus vdpret;
    vdpret = vdp_presentation_queue_target_create_x11(vdp_device, x11_window,
                                                      &vdp_target);
    if (vdpret != VDP_STATUS_OK) {
        fprintf(stderr,"Failed to create x11 target queue\n");
        return vdpret;
    }

    vdpret = vdp_presentation_queue_create(vdp_device, vdp_target, &vdp_queue);
    if (vdpret != VDP_STATUS_OK) {
        fprintf(stderr,"Failed to create queue\n");
        return vdpret;
    }
    fprintf(stdout,"Created presentation queue\n");
    return vdpret;
}

char *generate_garbage_buffer(int width, int height) {
    long i;
    char *buf = malloc(width * height);
    if (!buf) return NULL;
    for (i = 0; i < (width * height); i++) {
        buf[i] = (i & 0xFF);
    }
    return buf;
}

int main() {
    /*XEvent event;
    int retval;
    vid_width = 300;
    vid_height = 300;
    char *garbage;
    const char *info;
    VdpStatus vdpret = VDP_STATUS_OK;

    retval = init_x11();
    if (retval) {
        fprintf(stderr,"Failed to init X11");
        return -1;
    }
    vdpret = init_vdpau();
    if (vdpret != VDP_STATUS_OK) {
        fprintf(stderr,"Failed to init VDPAU");
        return -1;
    }
    vdpret = vdp_get_information_string(&info);
    fprintf(stdout,"%s\n",info);

    retval = init_decoder();
    if (retval != VDP_STATUS_OK) {
        fprintf(stderr,"Failed to init decoder");
        return -1;
    }

    vdpret = init_surfaces();
    if (vdpret != VDP_STATUS_OK) {
        fprintf(stderr, "Failed to init surfaces");
        return -1; 
    }
    fprintf(stdout,"VDP surfaces created\n");
    garbage = generate_garbage_buffer(WIDTH,HEIGHT);
    fprintf(stdout,"Generated some garbage\n");

    vdpret = init_video_mixer();
    if (vdpret != VDP_STATUS_OK) {
        fprintf(stderr,"Failed to create video mixer");
        return -1;
    }

    vdpret = init_presentation_queue();
    if (vdpret != VDP_STATUS_OK) {
        fprintf(stderr,"Failed to presentation queue\n");
        return -1;
    } 

    while(1) {
        XNextEvent(x11_display, &event);
        if (event.type == KeyPress) {
            XFreeGC(x11_display, x11_gc);
            XDestroyWindow(x11_display, x11_window);
            XCloseDisplay(x11_display);
            exit(0);
        }
    }*/

    init_vpdau_ctx();
}
