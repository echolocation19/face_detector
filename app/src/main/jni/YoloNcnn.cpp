#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>
#include <android/native_window.h>

#include <android/log.h>

#include <jni.h>

#define APPNAME "yoloxncnn.cpp"

#include <string>
#include <vector>

#include <platform.h>
#include <benchmark.h>

#include "YoloV5.h"

#include "NdkCamera.h"

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#if __ARM_NEON
#include <arm_neon.h>
#endif // __ARM_NEON

JNIEnv *ncnn_env;


static jint JNI_VERSION = JNI_VERSION_1_4;

JavaVM *javaVM_global;
jclass MainActivityClass;
jobject MainActivityObject; // to make non-static calls


static int draw_unsupported(cv::Mat &rgb) {
    const char text[] = "unsupported";

    int baseLine = 0;
    cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 1.0, 1, &baseLine);

    int y = (rgb.rows - label_size.height) / 2;
    int x = (rgb.cols - label_size.width) / 2;

    cv::rectangle(rgb, cv::Rect(cv::Point(x, y), cv::Size(label_size.width, label_size.height + baseLine)),
                  cv::Scalar(255, 255, 255), -1);

    cv::putText(rgb, text, cv::Point(x, y + label_size.height),
                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 0));

    return 0;
}

static int draw_fps(cv::Mat &rgb) {
    // resolve moving average
    float avg_fps = 0.f;
    {
        static double t0 = 0.f;
        static float fps_history[10] = {0.f};

        double t1 = ncnn::get_current_time();
        if (t0 == 0.f) {
            t0 = t1;
            return 0;
        }

        float fps = 1000.f / (t1 - t0);
        t0 = t1;

        for (int i = 9; i >= 1; i--) {
            fps_history[i] = fps_history[i - 1];
        }
        fps_history[0] = fps;

        if (fps_history[9] == 0.f) {
            return 0;
        }

        for (int i = 0; i < 10; i++) {
            avg_fps += fps_history[i];
        }
        avg_fps /= 10.f;
    }

    char text[32];
    sprintf(text, "FPS=%.2f", avg_fps);

    int baseLine = 0;
    cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);

    int y = 0;
    int x = rgb.cols - label_size.width;

    cv::rectangle(rgb, cv::Rect(cv::Point(x, y), cv::Size(label_size.width, label_size.height + baseLine)),
                  cv::Scalar(255, 255, 255), -1);

    cv::putText(rgb, text, cv::Point(x, y + label_size.height),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0));

    return 0;
}

static Yolox *g_yolox = 0;
static ncnn::Mutex lock;

class MyNdkCamera : public NdkCameraWindow {
public:
    virtual void on_image_render(cv::Mat &rgb) const;
};

static const char *class_names[] = {
        "glass", "mask", "mask_glass", "normal"
};


void MyNdkCamera::on_image_render(cv::Mat &rgb) const {
    {
        ncnn::MutexLockGuard g(lock);

        if (g_yolox) {
            std::vector<Object> objects;
            g_yolox->detect(rgb, objects);
            g_yolox->draw(rgb, objects);

            for (auto &object: objects) {
                __android_log_print(ANDROID_LOG_ERROR, APPNAME, "%s", class_names[object.label]);
            }
        } else {
            draw_unsupported(rgb);
        }
    }

    draw_fps(rgb);
}

static MyNdkCamera *g_camera = 0;
extern "C" {

JNIEXPORT jboolean JNICALL Java_com_example_facedefinition_YoloProvider_injectObjectReference(JNIEnv *env, jobject thiz,
                                                                                              jobject main_activity) {
    MainActivityObject = (jobject) env->NewGlobalRef(main_activity);
    return JNI_TRUE;
}

JNIEXPORT jint
JNI_OnLoad(JavaVM *vm, void *reserved) {
    __android_log_print(ANDROID_LOG_DEBUG, APPNAME, "JNI_OnLoad");

// Obtain the JNIEnv from the VM and confirm JNI_VERSION
    if (vm->GetEnv(reinterpret_cast <void **>(&ncnn_env), JNI_VERSION) != JNI_OK) {
        __android_log_print(ANDROID_LOG_ERROR, APPNAME,
                            "error  JNI_VERSION) != JNI_OK");
        return JNI_ERR;
    }
    javaVM_global = vm; // important. This variable is critical for success.
    jclass tempLocalClassRef;
    tempLocalClassRef = ncnn_env->FindClass("com/example/facedefinition/MainActivity");

// STEP 1/3 : Load the class id
    if (tempLocalClassRef == nullptr || ncnn_env->
            ExceptionOccurred()
            ) {
        ncnn_env->
                ExceptionClear();
        __android_log_print(ANDROID_LOG_ERROR,
                            APPNAME, "%s", "There was an error in JNI_OnLoad");
    }
// STEP 2/3 : Assign the ClassId as a Global Reference
    MainActivityClass = (jclass) ncnn_env->NewGlobalRef(tempLocalClassRef);
// STEP 3/3 : Delete the no longer needed local reference
    ncnn_env-> DeleteLocalRef(tempLocalClassRef);
    g_camera = new MyNdkCamera;
    return JNI_VERSION_1_4;
}

JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *reserved) {
    __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "JNI_OnUnload");

    {
        ncnn::MutexLockGuard g(lock);

        delete g_yolox;
        g_yolox = 0;
    }
    // clean up
    vm->GetEnv(reinterpret_cast<void **>(&ncnn_env), JNI_VERSION);
    ncnn_env->DeleteGlobalRef(MainActivityClass);
    ncnn_env->DeleteGlobalRef(MainActivityObject);

    delete g_camera;
    g_camera = 0;
}

// loadModel(AssetManager mgr, int modelid, int cpugpu);
JNIEXPORT jboolean JNICALL
Java_com_example_facedefinition_YoloProvider_loadModel(JNIEnv *env, jobject thiz, jobject assetManager) {
    AAssetManager *mgr = AAssetManager_fromJava(env, assetManager);

    __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "loadModel %p", mgr);

    const char *modeltypes[] = {"yolov5-lite"};
    const int target_sizes[] = {320};
    const char *modeltype = modeltypes[0];
    int target_size = target_sizes[0];
    bool use_gpu = true;
    // reload
    {
        ncnn::MutexLockGuard g(lock);
        if (!g_yolox)
            g_yolox = new Yolox;
        g_yolox->load(mgr, modeltype, target_size, use_gpu);
    }

    return JNI_TRUE;
}

// openCamera(int facing);
JNIEXPORT jboolean JNICALL
Java_com_example_facedefinition_YoloProvider_openCamera(JNIEnv *env, jobject thiz, jint facing) {
    if (facing < 0 || facing > 1)
        return JNI_FALSE;

    __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "openCamera %d", facing);

    g_camera->open((int) facing);

    return JNI_TRUE;
}

// closeCamera();
JNIEXPORT jboolean JNICALL Java_com_example_facedefinition_YoloProvider_closeCamera(JNIEnv *env, jobject thiz) {
    __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "closeCamera");

    g_camera->close();

    return JNI_TRUE;
}

// public native boolean setOutputWindow(Surface surface);
JNIEXPORT jboolean JNICALL
Java_com_example_facedefinition_YoloProvider_setOutputWindow(JNIEnv *env, jobject thiz, jobject surface) {
    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);

    __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "setOutputWindow %p", win);

    g_camera->set_window(win);

    return JNI_TRUE;
}
}